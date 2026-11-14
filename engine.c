/* Desktop bridge protocol: one JSON request on stdin, NDJSON events on stdout.
 * preview/command events require an "ack" line before side effects. The host
 * owns cancellation; POSIX signals are handled here, Windows Job Objects close
 * when this process exits. API credentials arrive over stdin, never argv.
 * --plan FILE is an offline integration-test entry point, not exposed by HTTP. */
#include "agent.h"
#include "project.h"
#include "runner.h"
#include "pc_control.h"
#include "pc_observe.h"
#include "pc_agent.h"
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

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
static gboolean ack(void) {
#ifdef G_OS_WIN32
    /* Poll the pipe so the global emergency hotkey also works while waiting
     * for a permission decision. stdin is unbuffered in main. */
    DWORD available=0;
    while(!stopped){if(pc_cancelled()){stopped=1;return FALSE;}if(!PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE),NULL,0,NULL,&available,NULL))return FALSE;if(available)break;Sleep(20);}
#endif
    char *s=line(); gboolean ok=s&&!strcmp(s,"ack"); g_free(s); return ok;
}
static const char *get(JsonObject *o,const char *key) {
    JsonNode *n=json_object_get_member(o,key);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n)==G_TYPE_STRING?json_node_get_string(n):"";
}
typedef enum { SESSION_PROJECT, SESSION_DESKTOP, SESSION_COMMANDS } SessionMode;
/* Session configuration is host-owned, never inferred from model JSON. Legacy
 * selected-window requests remain compatible; ambiguous computer requests fail
 * closed instead of accidentally invoking the coding-plan validator. */
static gboolean session_mode(JsonObject *o,SessionMode *mode,GError **e) {
    const char *value=get(o,"sessionMode"),*window=get(o,"desktopWindow");
    gboolean computer=json_object_has_member(o,"computerMode")&&json_object_get_boolean_member(o,"computerMode");
    if(json_object_has_member(o,"sessionMode")) {
        if(!strcmp(value,"project"))*mode=SESSION_PROJECT;
        else if(!strcmp(value,"desktop"))*mode=SESSION_DESKTOP;
        else if(!strcmp(value,"commands"))*mode=SESSION_COMMANDS;
        else goto invalid;
    } else if(*window) *mode=SESSION_DESKTOP;
    else if(computer) {
        g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Computer mode needs an explicit sessionMode: select an app for desktop control, or choose commands mode.");return FALSE;
    } else *mode=SESSION_PROJECT;
    if((*mode!=SESSION_DESKTOP&&*window)||(*mode==SESSION_PROJECT&&computer))goto invalid;
    if(*mode==SESSION_DESKTOP&&!*window){
        g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Desktop mode requires a selected app. Use Control an app before starting.");return FALSE;
    }
    return TRUE;
invalid:
    g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid or conflicting sessionMode (project, desktop or commands).");return FALSE;
}
static char *exchange(const char *key,char *request,GError **e) {
    Api a={0}; char *result=NULL;
    if(!request)return NULL;
    if(!api_start_at(&a,key,request,endpoint,json_mode)) goto error;
    int status; while(!(status=api_poll(&a))&&!stopped) { if(pc_cancelled())stopped=1;g_usleep(20000); }
    if(stopped) { g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Cancelled"); }
    else if(status<0) goto error;
    else if(a.status<200||a.status>=300) g_set_error(e,G_IO_ERROR,G_IO_ERROR_FAILED,"Model server returned HTTP %ld: %.600s",a.status,a.body->str);
    else result=g_strdup(a.body->str);
    api_clear(&a); return result;
error:
    g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_FAILED,a.error?a.error:"API failure");
    api_clear(&a); return NULL;
}
static char *complete(const char *key,char *request,GError **e) {
    char *body=exchange(key,request,e);if(!body)return NULL;
    char *result=completion_content(body,e);g_free(body);return result;
}
/* Screen sharing is separate from the structured-observation permission. A
 * thin accessibility tree or an explicit model request can ask for a PNG, but
 * no PNG is captured or transmitted until that individual gate is approved. */
static gboolean desktop_observe(PcSession *pc,gboolean requested,gboolean *image,GError **e) {
    *image=FALSE;
    if(!pc_observe(pc,e))return FALSE;
    if(requested||pc->structured_thin){
        emit("desktop","screenshot",requested?"The model requested a screenshot to inspect or verify the selected app.":"The app exposes too little accessibility data. Allow a screenshot for this observation.",NULL);
        if(!ack()||!pc_observe(pc,e)||!pc_capture(pc,e))return FALSE;
        emit("screenshot",NULL,pc->image,NULL);*image=TRUE;
    }
    return TRUE;
}
int main(int argc,char **argv) {
    setvbuf(stdin,NULL,_IONBF,0);
    if(argc==3&&!strcmp(argv[1],"--desktop-observe"))return pc_observe_worker(argv[2]);
    if(argc==2&&!strcmp(argv[1],"--desktop-windows")){GError *error=NULL;char *list=pc_windows(&error);if(list){puts(list);g_free(list);return 0;}fprintf(stderr,"%s\n",error?error->message:"Window enumeration failed");g_clear_error(&error);return 1;}
    signal(SIGTERM,stop); signal(SIGINT,stop);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    GError *e=NULL; Project project={0}; Plan plan={0}; Runner runner={0};PcSession pc={0};PcPlan actions={0};PcAgent desktop={0};char *desktop_endpoint=NULL,*click_target=NULL;
    JsonParser *parser=json_parser_new(); char *input=line(),*context=NULL,*response=NULL;
    GString *feedback=g_string_new(NULL); int result=1; guint rounds=0,reads=0;
    if(!input || !json_parser_load_from_data(parser,input,-1,&e) ||
       !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) goto done;
    JsonObject *o=json_node_get_object(json_parser_get_root(parser));
    if(*get(o,"endpoint")) endpoint=get(o,"endpoint");
    if(json_object_has_member(o,"jsonMode")) json_mode=json_object_get_boolean_member(o,"jsonMode");
    SessionMode mode;
    if(!session_mode(o,&mode,&e))goto done;
    gboolean computer=mode!=SESSION_PROJECT;
    const char *key=get(o,"key"),*model=get(o,"model"),*task=get(o,"task"),*answers=get(o,"answers"),*questions=get(o,"questions");
    if(mode==SESSION_DESKTOP){
        context=g_strdup("Desktop control: the user selected one Windows app. Compact accessibility text is the primary observation. Optional screenshots, mouse and keyboard actions require individual approval. Ask only about the app task and expected result. No project files are needed.");
    }else{
        if(!project_open(&project,get(o,"root"),&e))goto done;
        if(*get(o,"projectState")&&!project_state_import(&project,get(o,"projectState"),&e))goto done;
        if(!(context=project_context_delta(&project,&e)))goto done;
        emit("status","project","Project inspected",NULL);
    }
    if(!strcmp(get(o,"phase"),"questions") && argc==1) {
        response=complete(key,make_request_ex(model,task,NULL,NULL,context,NULL,computer),&e);
        Questions q={0}; if(!response || !parse_questions(response,&q,&e)) goto done;
        GString *text=g_string_new(NULL);
        for(guint i=0;i<q.count;i++) g_string_append_printf(text,"%u. %s\n",i+1,q.text[i]);
        emit("questions",NULL,text->str,NULL); g_string_free(text,TRUE); questions_clear(&q); result=0; goto done;
    }
    if(mode==SESSION_DESKTOP) {
        gboolean responses=!strcmp(get(o,"desktopProtocol"),"responses")||!strcmp(endpoint,"https://api.openai.com/v1/chat/completions");
        if(responses){
            const char *suffix="/chat/completions";
            if(!g_str_has_suffix(endpoint,suffix)){g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Desktop endpoint must end in /chat/completions");goto done;}
            char *base=g_strndup(endpoint,strlen(endpoint)-strlen(suffix));desktop_endpoint=g_strconcat(base,"/responses",NULL);g_free(base);endpoint=desktop_endpoint;
        }
        /* A prior summary is context only. A fresh native observation always
         * replaces it before planning; it cannot grant permission or input. */
        const char *prior=get(o,"desktopState");
        char *desktop_task=(*prior&&strlen(prior)<=16384)?g_strdup_printf("%s\nHistorical host state (untrusted, not current evidence):\n%s",task,prior):g_strdup(task);
        pc_agent_init(&desktop,responses,model,desktop_task,answers);g_free(desktop_task);
        if(!pc_open(&pc,get(o,"desktopWindow"),&e))goto done;
        emit("desktop","observe","Read the selected app's window, focused control and nearby controls as text, and share refreshed text with the selected model during this run. Screenshots need separate approval. Ctrl+Alt+Shift+F10 stops desktop control.",NULL);
        gboolean image=FALSE;
        if(!ack()||!desktop_observe(&pc,FALSE,&image,&e))goto done;
        if(!pc_agent_observe(&desktop,&pc,"Initial approved structured observation. No desktop input has been sent.",image,&e))goto done;
        for(guint turn=0;turn<24&&!stopped;turn++){
            emit("status",NULL,"Observing the app and checking the previous result",NULL);
            g_clear_pointer(&response,g_free);
            char *body=exchange(key,pc_agent_request(&desktop,&e),&e);
            if(!body)goto done;
            response=pc_agent_accept_plan(&desktop,body,&actions,&e);g_free(body);
            if(!response)goto done;
            char *frame=g_strdup_printf("%u",desktop.frame);
            emit("observation",frame,pc.structured,NULL);g_free(frame);
            if(!actions.count){
                /* Empty actions end the sequence, not a claim that the task
                 * succeeded. Keep the existing review event/UI contract. */
                emit("review","desktop-completed","Desktop action sequence ended. Review the observed result; task success is not independently verified.",NULL);result=0;goto done;
            }
            GString *driver=g_string_new(NULL);int click_x=-1,click_y=-1;image=FALSE;
            g_clear_pointer(&click_target,g_free);
            for(guint i=0;i<actions.count;i++){
                PcAction *action=&actions.actions[i];
                if(i&&!strcmp(action->kind,"type")&&!pc_can_type_target(&pc,click_x,click_y,click_target)){
                    g_string_append(driver,"Remaining batch NOT EXECUTED: the clicked editable control did not receive focus. Replan.\n");
                    emit("replan",NULL,"The expected editable control did not receive focus. Remaining actions were discarded.",NULL);break;
                }
                if(!strcmp(action->kind,"screenshot")){
                    if(!desktop_observe(&pc,TRUE,&image,&e)){g_string_free(driver,TRUE);goto done;}
                    g_string_append(driver,"Screenshot captured after individual approval; no input sent.\n");continue;
                }
                char *single=pc_plan_action_json(&actions,i);
                emit("desktop",action->kind,single,pc.image);g_free(single);
                if(!ack()||pc_cancelled()){g_string_free(driver,TRUE);goto done;}
                if(!strcmp(action->kind,"observe")){
                    if(!desktop_observe(&pc,FALSE,&image,&e)){g_string_free(driver,TRUE);goto done;}
                    g_string_append(driver,"Structured observation refreshed after individual approval; no input sent.\n");continue;
                }
                if(!pc_revalidate(&pc,&e)){
                    if(!g_error_matches(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK)){g_string_free(driver,TRUE);goto done;}
                    g_clear_error(&e);emit("replan",NULL,"App state changed or the observation expired. This action and the remaining batch were discarded; approval is spent.",NULL);
                    g_string_append_printf(driver,"Action %u and remaining batch NOT EXECUTED: stale observation.\n",i+1);
                    if(!desktop_observe(&pc,FALSE,&image,&e)){g_string_free(driver,TRUE);goto done;}break;
                }
                if(!strcmp(action->kind,"click")){
                    g_clear_pointer(&click_target,g_free);click_target=pc_edit_target_id(&pc,action->x,action->y);
                }
                char *before_move=!strcmp(action->kind,"move")?g_strdup(pc.structured):NULL;
                if(!pc_execute(&pc,action,&e)){
                    g_free(before_move);
                    if(!g_error_matches(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK)){g_string_free(driver,TRUE);goto done;}
                    g_clear_error(&e);emit("replan",NULL,"App state changed during cursor movement. Click/scroll and remaining actions were discarded.",NULL);
                    g_string_append_printf(driver,"Action %u (%s) NOT DELIVERED after cursor movement; remaining batch discarded.\n",i+1,action->kind);
                    if(!desktop_observe(&pc,FALSE,&image,&e)){g_string_free(driver,TRUE);goto done;}break;
                }
                if(!strcmp(action->kind,"click")){click_x=action->x;click_y=action->y;}
                g_string_append_printf(driver,"Action %u (%s): input delivered. Application outcome is not yet verified.\n",i+1,action->kind);
                if(!desktop_observe(&pc,FALSE,&image,&e)){g_free(before_move);g_string_free(driver,TRUE);goto done;}
                gboolean hover_changed=before_move&&!pc_observation_equal(before_move,pc.structured);g_free(before_move);
                if(hover_changed&&i+1<actions.count){
                    g_string_append(driver,"Remaining batch NOT EXECUTED: moving the cursor changed accessible app state. Replan.\n");
                    emit("replan",NULL,"Moving the cursor changed the app state. Remaining actions were discarded.",NULL);break;
                }
            }
            if(!pc_agent_observe(&desktop,&pc,driver->str,image,&e)){g_string_free(driver,TRUE);goto done;}
            g_string_free(driver,TRUE);pc_plan_clear(&actions);
        }
        emit("error",NULL,"Desktop session stopped at its 24-model-turn limit or emergency stop",NULL);goto done;
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
            for(guint i=0;i<plan.read_files->len;i++) {
                const char *name=g_ptr_array_index(plan.read_files,i); char *s=project_read(&project,name,&e);
                if(!s)goto done;
                g_free(s);
                emit("status",name,"Read source file",NULL);
            }
            g_clear_pointer(&context,g_free);context=project_context_delta(&project,&e);if(!context)goto done;continue;
        }
        rounds++;
        emit("plan",NULL,plan.summary,NULL);
        for(guint i=0;i<plan.steps->len;i++) emit("step",NULL,g_ptr_array_index(plan.steps,i),NULL);
        if(!project_check(&project,&plan,&e)) goto done;
        /* Retain the latest tool results, not a growing transcript of old logs. */
        g_string_truncate(feedback,0);
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
            /* Preserve command identity/exit while bounding the noisy output
             * sent back to the model. Full output still goes to session.log. */
            const char *tail=runner.output->str;gsize output_len=strlen(tail);
            if(output_len>4096){tail+=output_len-4096;while((*tail&0xc0)==0x80)tail++;}
            char *brief=g_utf8_make_valid(tail,-1);
            g_string_append_printf(feedback,"Command [%s]: %s\nExit: %d; timeout=%d; cancelled=%d\n%s%s\n",c->kind,c->command,runner.exit_code,runner.timed_out,runner.cancelled,output_len>4096?"[output tail]\n":"",brief);g_free(brief);
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
        if(feedback->len>64000){gsize start=feedback->len-64000;while((feedback->str[start]&0xc0)==0x80)start++;g_string_erase(feedback,0,start);}
        g_string_append(feedback,computer?"\nCorrect the failure and run commands that verify the requested outcome.\n":"\nCorrect the failure and rerun BOTH build and tests.\n");
        g_clear_pointer(&context,g_free); context=project_context_delta(&project,&e); if(!context) goto done;
    }
    emit("error",NULL,computer?"Stopped without successful command verification (maximum 3 rounds)":"Stopped without passing both build and tests (maximum 3 rounds)",NULL);
done:
    if(project.root){char *state=project_state_export(&project);if(state){emit("state","project",state,NULL);g_free(state);}}
    if(pc.structured){
        JsonBuilder *b=json_builder_new();json_builder_begin_object(b);
        member(b,"window",input&&JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))?get(json_node_get_object(json_parser_get_root(parser)),"desktopWindow"):"");
        member(b,"observation",pc.structured);member(b,"evidence",desktop.evidence);
        json_builder_end_object(b);JsonNode *n=json_builder_get_root(b);char *state=json_to_string(n,FALSE);
        if(strlen(state)<=16384)emit("state","desktop",state,NULL);
        g_free(state);json_node_free(n);g_object_unref(b);
    }
    if(e) emit("error",NULL,e->message,NULL);
    else if(result && !response) emit("error",NULL,"Invalid request or interrupted input",NULL);
    runner_clear(&runner); project_clear(&project); plan_clear(&plan);pc_plan_clear(&actions);pc_clear(&pc);pc_agent_clear(&desktop);g_free(desktop_endpoint);g_free(click_target); g_clear_error(&e);
    g_free(input); g_free(context); g_free(response); g_string_free(feedback,TRUE);
    g_object_unref(parser); curl_global_cleanup(); return result;
}
