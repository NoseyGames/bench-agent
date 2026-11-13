/* Desktop bridge protocol: one JSON request on stdin, NDJSON events on stdout.
 * preview/command events require an "ack" line before side effects. The host
 * owns cancellation; POSIX signals are handled here, Windows Job Objects close
 * when this process exits. API credentials arrive over stdin, never argv.
 * --plan FILE is an offline integration-test entry point, not exposed by HTTP. */
#include "agent.h"
#include "project.h"
#include "runner.h"
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

static volatile sig_atomic_t stopped;
static const char *endpoint="https://api.openai.com/v1/chat/completions";
static gboolean json_mode=TRUE;
static void stop(int sig) { (void)sig; stopped=1; }
static void member(JsonBuilder *b,const char *key,const char *value) {
    json_builder_set_member_name(b,key); json_builder_add_string_value(b,value?value:"");
}
static void emit(const char *type,const char *name,const char *text,const char *before) {
    JsonBuilder *b=json_builder_new(); json_builder_begin_object(b);
    member(b,"type",type); member(b,"name",name); member(b,"text",text); member(b,"before",before);
    json_builder_end_object(b); JsonNode *n=json_builder_get_root(b);
    JsonGenerator *g=json_generator_new(); json_generator_set_root(g,n);
    char *s=json_generator_to_data(g,NULL); puts(s); fflush(stdout);
    g_free(s); json_node_free(n); g_object_unref(g); g_object_unref(b);
}
static char *line(void) {
    GString *s=g_string_new(NULL); int c;
    while(!stopped && (c=getchar())!=EOF && c!='\n') {
        if(s->len>=3*1024*1024) { g_string_free(s,TRUE); return NULL; }
        g_string_append_c(s,(char)c);
    }
    if(!s->len || stopped) { g_string_free(s,TRUE); return NULL; }
    return g_string_free(s,FALSE);
}
static gboolean ack(void) { char *s=line(); gboolean ok=s&&!strcmp(s,"ack"); g_free(s); return ok; }
static const char *get(JsonObject *o,const char *key) {
    JsonNode *n=json_object_get_member(o,key);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n)==G_TYPE_STRING?json_node_get_string(n):"";
}
static char *complete(const char *key,char *request,GError **e) {
    Api a={0}; char *result=NULL;
    if(!api_start_at(&a,key,request,endpoint,json_mode)) goto error;
    int status; while(!(status=api_poll(&a))&&!stopped) g_usleep(20000);
    if(stopped) { g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Cancelled"); }
    else if(status<0) goto error;
    else if(a.status<200||a.status>=300) g_set_error(e,G_IO_ERROR,G_IO_ERROR_FAILED,"Model server returned HTTP %ld: %.600s",a.status,a.body->str);
    else result=completion_content(a.body->str,e);
    api_clear(&a); return result;
error:
    g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_FAILED,a.error?a.error:"API failure");
    api_clear(&a); return NULL;
}
int main(int argc,char **argv) {
    signal(SIGTERM,stop); signal(SIGINT,stop);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    GError *e=NULL; Project project={0}; Plan plan={0}; Runner runner={0};
    JsonParser *parser=json_parser_new(); char *input=line(),*context=NULL,*response=NULL;
    GString *feedback=g_string_new(NULL); int result=1; guint rounds=0,reads=0;
    if(!input || !json_parser_load_from_data(parser,input,-1,&e) ||
       !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) goto done;
    JsonObject *o=json_node_get_object(json_parser_get_root(parser));
    if(*get(o,"endpoint")) endpoint=get(o,"endpoint");
    if(json_object_has_member(o,"jsonMode")) json_mode=json_object_get_boolean_member(o,"jsonMode");
    gboolean computer=json_object_has_member(o,"computerMode") && json_object_get_boolean_member(o,"computerMode");
    const char *key=get(o,"key"),*model=get(o,"model"),*task=get(o,"task"),*answers=get(o,"answers"),*questions=get(o,"questions");
    if(!project_open(&project,get(o,"root"),&e) || !(context=project_context(&project,&e))) goto done;
    emit("status","project","Project inspected",NULL);
    if(!strcmp(get(o,"phase"),"questions") && argc==1) {
        response=complete(key,make_request_ex(model,task,NULL,NULL,context,NULL,computer),&e);
        Questions q={0}; if(!response || !parse_questions(response,&q,&e)) goto done;
        GString *text=g_string_new(NULL);
        for(guint i=0;i<q.count;i++) g_string_append_printf(text,"%u. %s\n",i+1,q.text[i]);
        emit("questions",NULL,text->str,NULL); g_string_free(text,TRUE); questions_clear(&q); result=0; goto done;
    }
    while(!stopped && rounds<3) {
        g_clear_pointer(&response,g_free); plan_clear(&plan);
        emit("status","planning","Planning from the current project and command results",NULL);
        if(argc==3 && !strcmp(argv[1],"--plan")) {
            if(rounds || !g_file_get_contents(argv[2],&response,NULL,&e)) break;
        } else response=complete(key,make_request_ex(model,task,questions,answers,context,feedback->str,computer),&e);
        if(!response || !parse_plan(response,&plan,&e)) goto done;
        if(plan.read_files->len) {
            if(++reads>3) { emit("error",NULL,"Additional file read limit reached",NULL); goto done; }
            GString *extra=g_string_new(context);
            for(guint i=0;i<plan.read_files->len;i++) {
                const char *name=g_ptr_array_index(plan.read_files,i); char *s=project_read(&project,name,&e);
                if(!s) { g_string_free(extra,TRUE); goto done; }
                g_string_append_printf(extra,"\n--- FILE %s ---\n%s\n",name,s); g_free(s);
                if(extra->len>1500*1024) { g_string_free(extra,TRUE); emit("error",NULL,"Source context limit reached",NULL); goto done; }
                emit("status",name,"Read source file",NULL);
            }
            g_free(context); context=g_string_free(extra,FALSE); continue;
        }
        rounds++;
        emit("plan",NULL,plan.summary,NULL);
        for(guint i=0;i<plan.steps->len;i++) emit("step",NULL,g_ptr_array_index(plan.steps,i),NULL);
        if(!project_check(&project,&plan,&e)) goto done;
        for(guint i=0;i<plan.count;i++) {
            AgentFile *f=&plan.files[i]; char *old=NULL,*path=g_build_filename(project.root,f->name,NULL);
            g_file_get_contents(path,&old,NULL,NULL); g_free(path);
            emit("preview",f->name,f->content,old); g_free(old);
            if(!ack()) goto done;
            if(!project_write(&project,f,&e)) goto done;
            emit("saved",f->name,"Saved (original backed up when present)",NULL);
        }
        gboolean built=FALSE,tested=FALSE,passed=TRUE;
        for(guint i=0;i<plan.commands->len;i++) {
            AgentCommand *c=g_ptr_array_index(plan.commands,i);
            emit("command",c->kind,c->command,NULL); if(!ack()) goto done;
            if(!runner_start(&runner,project.root,c->command,c->timeout,&e)) goto done;
            guint64 seen=0;
            do {
                if(stopped) runner_cancel(&runner);
                gboolean finished=runner_poll(&runner);
                if(runner.output_bytes!=seen) {
                    /* Send the bounded tail as a replacement, avoiding UTF-8 splits. */
                    char *valid=g_utf8_make_valid(runner.output->str,-1);
                    emit("output",c->kind,valid,NULL); g_free(valid); seen=runner.output_bytes;
                }
                if(finished) break;
                g_usleep(25000);
            } while(TRUE);
            char *log=g_strdup_printf("Command [%s]: %s\nExit: %d; timeout=%d; cancelled=%d\n%s\n",
                c->kind,c->command,runner.exit_code,runner.timed_out,runner.cancelled,runner.output->str);
            char *valid=g_utf8_make_valid(log,-1); g_free(log);
            g_string_append(feedback,valid);
            if(!project_log(&project,valid,&e)) { g_free(valid); goto done; }
            g_free(valid);
            char *exit=g_strdup_printf("Exit %d%s",runner.exit_code,runner.timed_out?" (timed out)":"");
            emit("exit",c->kind,exit,NULL); g_free(exit);
            gboolean ok=runner.exit_code==0&&!runner.timed_out&&!runner.cancelled;
            if(!ok) passed=FALSE;
            if(ok&&!strcmp(c->kind,"build")) built=TRUE;
            if(ok&&!strcmp(c->kind,"test")) tested=TRUE;
            runner_clear(&runner); if(stopped) goto done;
        }
        if(passed&&((built&&tested)||(computer&&plan.commands->len>0))) { result=0; emit("success",project.run_dir,computer?"Approved commands completed successfully; review their output for task-specific verification":"Build and tests passed",NULL); goto done; }
        emit("status","repair",computer?"Command verification incomplete; requesting repair":"Build/test verification incomplete; requesting repair",NULL);
        if(feedback->len>192000) g_string_erase(feedback,0,feedback->len-192000);
        g_string_append(feedback,computer?"\nCorrect the failure and run commands that verify the requested outcome.\n":"\nCorrect the failure and rerun BOTH build and tests.\n");
        g_clear_pointer(&context,g_free); context=project_context(&project,&e); if(!context) goto done;
    }
    emit("error",NULL,computer?"Stopped without successful command verification (maximum 3 rounds)":"Stopped without passing both build and tests (maximum 3 rounds)",NULL);
done:
    if(e) emit("error",NULL,e->message,NULL);
    else if(result && !response) emit("error",NULL,"Invalid request or interrupted input",NULL);
    runner_clear(&runner); project_clear(&project); plan_clear(&plan); g_clear_error(&e);
    g_free(input); g_free(context); g_free(response); g_string_free(feedback,TRUE);
    g_object_unref(parser); curl_global_cleanup(); return result;
}
