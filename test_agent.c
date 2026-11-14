#include "agent.h"
#include "project.h"
#include "runner.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
static char *self;

static const char *valid_plan="{\"summary\":\"Ready\",\"steps\":[\"Write code\"],\"files\":[{\"name\":\"main.c\",\"content\":\"int main(void) { return 0; }\\n\"}],\"commands\":[],\"read_files\":[]}";
static void filenames(void) {
    const char *bad[]={"../escape.c","/tmp/x","C:\\x","a/b","a\\b","a:stream","CON","nul.c","LPT9.txt","COM1.c",".hidden","x.","",NULL};
    for(int i=0;bad[i];i++) g_assert_false(safe_filename(bad[i]));
    g_assert_true(safe_filename("main.c")); g_assert_true(safe_filename("README.md"));
}
static void questions(void) {
    Questions q={0}; GError *e=NULL;
    g_assert_true(parse_questions("{\"questions\":[\"Platform?\",\"Language?\",\"Output?\"]}",&q,&e));
    g_assert_cmpuint(q.count,==,3); questions_clear(&q);
    const char *bad[]={"[]","{\"questions\":[\"Only one\"]}","{\"questions\":[null,3]}","{}","not JSON",NULL};
    for(int i=0;bad[i];i++) { g_assert_false(parse_questions(bad[i],&q,&e)); g_assert_nonnull(e); g_clear_error(&e); }
}
static void plans(void) {
    Plan p={0}; GError *e=NULL;
    g_assert_true(parse_plan(valid_plan,&p,&e)); g_assert_cmpuint(p.count,==,1);
    g_assert_cmpstr(p.files[0].name,==,"main.c"); plan_clear(&p);
    const char *bad[]={"{}","{\"summary\":1}",
        "{\"summary\":\"x\",\"steps\":[\"x\"],\"files\":[{\"name\":\"../x\",\"content\":\"a\"}]}",
        "{\"summary\":\"x\",\"steps\":[\"x\"],\"files\":[{\"name\":\"a.c\",\"content\":\"a\"},{\"name\":\"A.c\",\"content\":\"b\"}]}",
        "{\"summary\":\"x\",\"steps\":[\"x\"],\"files\":[{\"name\":\"a\",\"content\":null}]}",NULL};
    for(int i=0;bad[i];i++) { g_assert_false(parse_plan(bad[i],&p,&e)); g_assert_nonnull(e); g_clear_error(&e); plan_clear(&p); }
}
static void coding_schema_isolation(void) {
    GError *e=NULL; Plan p={0};
    const char *bad[]={
        "{\"actions\":[{\"name\":\"observe\",\"args\":{}}]}",
        "{\"summary\":\"mixed\",\"steps\":[\"read\"],\"files\":[],\"commands\":[],\"read_files\":[\"main.c\"],\"actions\":[{\"name\":\"click\",\"args\":{\"x\":1,\"y\":2}}]}",
        "{\"summary\":\"extra file field\",\"steps\":[\"write\"],\"files\":[{\"name\":\"main.c\",\"content\":\"x\",\"actions\":[]}],\"commands\":[],\"read_files\":[]}",
        "{\"summary\":\"extra command field\",\"steps\":[\"check\"],\"files\":[],\"commands\":[{\"kind\":\"inspect\",\"command\":\"echo check\",\"timeout_seconds\":10,\"args\":{}}],\"read_files\":[]}",
        "{\"summary\":\"read and write\",\"steps\":[\"mixed\"],\"files\":[{\"name\":\"main.c\",\"content\":\"x\"}],\"commands\":[],\"read_files\":[\"main.c\"]}",
        "{\"summary\":\"read and execute\",\"steps\":[\"mixed\"],\"files\":[],\"commands\":[{\"kind\":\"inspect\",\"command\":\"echo check\",\"timeout_seconds\":10}],\"read_files\":[\"main.c\"]}",
        NULL
    };
    for(guint i=0;bad[i];i++) {
        g_assert_false(parse_plan(bad[i],&p,&e)); g_assert_nonnull(e);
        g_assert_cmpuint(p.count,==,0); g_assert_null(p.summary); g_assert_null(p.commands);
        g_clear_error(&e);
    }
    /* The same validator continues to accept genuine coding plans. */
    g_assert_true(parse_plan(valid_plan,&p,&e)); g_assert_cmpuint(p.count,==,1); plan_clear(&p);
}
static void coding_plan_atomicity(void) {
    GError *e=NULL; Plan p={0};
    g_assert_true(parse_plan(valid_plan,&p,&e));
    char *original_content=p.files[0].content;
    GPtrArray *original_steps=p.steps;
    /* An invalid tail must not expose even the valid first file or command. */
    const char *bad[]={
        "{\"summary\":\"partial files\",\"steps\":[\"replace\"],\"files\":[{\"name\":\"first.c\",\"content\":\"first\"},{\"name\":\"second.c\",\"content\":null}],\"commands\":[],\"read_files\":[]}",
        "{\"summary\":\"partial commands\",\"steps\":[\"run\"],\"files\":[],\"commands\":[{\"kind\":\"inspect\",\"command\":\"echo first\",\"timeout_seconds\":10},{\"kind\":\"click\",\"command\":\"echo second\",\"timeout_seconds\":10}],\"read_files\":[]}",
        "{\"summary\":\"truncated\",\"steps\":[\"replace\"],\"files\":[{\"name\":\"first.c\",\"content\":\"first\"}],\"commands\":[],\"read_files\":[",
        NULL
    };
    for(guint i=0;bad[i];i++) {
        g_assert_false(parse_plan(bad[i],&p,&e)); g_assert_nonnull(e); g_clear_error(&e);
        g_assert_true(p.files[0].content==original_content); g_assert_true(p.steps==original_steps);
        g_assert_cmpstr(p.summary,==,"Ready"); g_assert_cmpstr(p.files[0].name,==,"main.c");
        g_assert_cmpuint(p.count,==,1); g_assert_cmpuint(p.commands->len,==,0);
    }
    plan_clear(&p);
}
static void strict_json(void) {
    GError *e=NULL;
    const char *bad[]={
        "{\"actions\":[]/* comment */}", "{\"actions\":[],}", "{'actions':[]}",
        "{\"actions\":[],\"actions\":[]}", "{\"a\":1,\"\\u0061\":2}",
        "{\"actions\":[]}{}", "{\"actions\":[]} trailing", "{\"x\":\"a\\u0000b\"}",
        "{\"x\":01}", "{\"x\":1.}", "{\"x\":1e}", "{\"x\":+1}",
        "{\"x\":\"\\u12\"}", "{\"x\":\"\\q\"}", "[true,]", "{\"x\":tru}",
        NULL
    };
    for(guint i=0;bad[i];i++) {
        g_test_message("Strict JSON fixture %u",i);
        g_assert_false(agent_json_strict(bad[i],&e));g_assert_nonnull(e);g_clear_error(&e);
    }
    const char *good[]={
        "{\"left\":{\"same\":1},\"right\":{\"same\":2}}",
        "{\"a\":[{\"b\":1},{}],\"s\":\"/* ordinary text */ \\\" \\u96ea\",\"n\":-10.5e+2,\"b\":true,\"z\":null}",
        "{\"literal\":\"\\\\u0000\"}", valid_plan, NULL
    };
    for(guint i=0;good[i];i++){g_assert_true(agent_json_strict(good[i],&e));g_assert_no_error(e);}
    GString *deep=g_string_new(NULL);
    for(guint i=0;i<65;i++)g_string_append_c(deep,'[');
    for(guint i=0;i<65;i++)g_string_append_c(deep,']');
    g_assert_false(agent_json_strict(deep->str,&e));g_assert_nonnull(e);g_clear_error(&e);g_string_free(deep,TRUE);
}
static void strict_coding_json(void) {
    GError *e=NULL;Plan p={0};
    g_assert_true(parse_plan(valid_plan,&p,&e));char *original=p.summary;
    char *prefix=g_strndup(valid_plan,strlen(valid_plan)-1);
    char *bad[]={
        g_strconcat(prefix,"/* comment */}",NULL),
        g_strconcat(prefix,",}",NULL),
        g_strconcat(prefix,",\"summary\":\"duplicate\"}",NULL),
        g_strconcat(prefix,",\"\\u0073ummary\":\"escaped duplicate\"}",NULL),
        g_strconcat(valid_plan,"{}",NULL),
        g_strdup("{'summary':'read','steps':['read'],'files':[],'commands':[],'read_files':['main.c']}"),
        NULL
    };
    for(guint i=0;bad[i];i++) {
        g_assert_false(parse_plan(bad[i],&p,&e));g_assert_nonnull(e);g_clear_error(&e);
        g_assert_true(p.summary==original);g_assert_cmpuint(p.count,==,1);g_free(bad[i]);
    }
    g_free(prefix);plan_clear(&p);
}
static void envelope(void) {
    GError *e=NULL;
    char *s=completion_content("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"héllo\"}}]}",&e);
    g_assert_cmpstr(s,==,"héllo"); g_free(s);
    const char *bad[]={"{}","{\"choices\":[null]}",
        "{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"content\":\"partial\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":null,\"refusal\":\"no\"}}]}",NULL};
    for(int i=0;bad[i];i++) { g_assert_null(completion_content(bad[i],&e)); g_assert_nonnull(e); g_clear_error(&e); }
}
static void request_json(void) {
    char *request=make_request("test-model","quotes \" and newline\n and 雪","Q?","answer\\",NULL,NULL);
    JsonParser *p=json_parser_new(); GError *e=NULL;
    g_assert_true(json_parser_load_from_data(p,request,-1,&e));
    JsonObject *o=json_node_get_object(json_parser_get_root(p));
    JsonArray *m=json_object_get_array_member(o,"messages");
    g_assert_cmpuint(json_array_get_length(m),==,5);
    g_assert_cmpstr(json_object_get_string_member(json_array_get_object_element(m,1),"content"),==,"quotes \" and newline\n and 雪");
    g_free(request); g_object_unref(p);
}
static void disk(void) {
    GError *e=NULL; char *dir=g_dir_make_tmp("bench-test-XXXXXX",&e); g_assert_nonnull(dir);
    AgentFile f={"main.c","hello\n雪\n"}; g_assert_true(save_file(dir,&f,&e));
    char *path=g_build_filename(dir,f.name,NULL), *read=NULL;
    g_assert_true(g_file_get_contents(path,&read,NULL,&e)); g_assert_cmpstr(read,==,f.content); g_free(read);
    g_assert_false(save_file(dir,&f,&e)); g_assert_nonnull(e); g_clear_error(&e);
    f.name="../escape"; g_assert_false(save_file(dir,&f,&e)); g_clear_error(&e);
    g_assert_cmpint(g_remove(path),==,0); g_assert_cmpint(g_rmdir(dir),==,0); g_free(path); g_free(dir);
}
static void limits(void) {
    GError *e=NULL; Plan p={0}; char *s=g_malloc0(MAX_RESPONSE_BYTES+2);
    memset(s,'a',MAX_RESPONSE_BYTES+1);
    g_assert_false(parse_plan(s,&p,&e)); g_assert_nonnull(e); g_clear_error(&e); g_free(s);
    g_assert_false(parse_plan("\xff",&p,&e)); g_clear_error(&e);
}
static void api_lifetime(void) {
    Api a={0};
    g_assert_false(api_start(&a,NULL,g_strdup("{}"))); g_assert_nonnull(a.error); api_clear(&a);
    g_assert_false(api_start(&a,"bad\nheader",g_strdup("{}"))); api_clear(&a);
    g_assert_true(api_start(&a,"test-key-not-sent",g_strdup("{}")));
    /* Cancel before polling: never sends the fixture key to any server. */
    api_clear(&a); api_clear(&a);
}
static void paths(void) {
    const char *bad[]={"../x","src/../../x","/abs","a//b","a/./b","a\\b","a:NUL","src/NUL.txt",".git/config",".env","src/secrets.json","folder./x",NULL};
    for(int i=0;bad[i];i++) g_assert_false(project_path_valid(bad[i]));
    g_assert_true(project_path_valid("src/nested/main.c"));
    g_assert_true(project_path_valid(".github/workflows/test.yml"));
    g_assert_true(project_path_valid("folder with spaces/雪.c"));
}
static void project_edits(void) {
    GError *e=NULL; Project p={0}; char *dir=g_dir_make_tmp("bench-project-XXXXXX",&e);
    g_assert_nonnull(dir); char *path=g_build_filename(dir,"existing.c",NULL), *secret=g_build_filename(dir,".env",NULL);
    g_assert_true(g_file_set_contents(path,"original\n",-1,&e));
    g_assert_true(g_file_set_contents(secret,"DO_NOT_SEND_THIS",-1,&e));
    g_assert_true(project_open(&p,dir,&e)); char *context=project_context(&p,&e);
    g_assert_nonnull(strstr(context,"original")); g_assert_null(strstr(context,"DO_NOT_SEND_THIS")); g_free(context);
    AgentFile f={"existing.c","edited\n"}; g_assert_true(project_write(&p,&f,&e));
    char *backup=g_build_filename(p.run_dir,"original","existing.c",NULL), *data=NULL;
    g_assert_true(g_file_get_contents(backup,&data,NULL,&e)); g_assert_cmpstr(data,==,"original\n"); g_free(data);
    f.content="edited again\n"; g_assert_true(project_write(&p,&f,&e));
    g_assert_true(g_file_get_contents(backup,&data,NULL,&e)); g_assert_cmpstr(data,==,"original\n"); g_free(data);
    g_assert_true(g_file_set_contents(path,"external edit\n",-1,&e));
    g_assert_false(project_write(&p,&f,&e)); g_assert_nonnull(strstr(e->message,"changed")); g_clear_error(&e);
    f.name="src/new.c"; f.content="new\n"; g_assert_true(project_write(&p,&f,&e));
    char *unseen=g_build_filename(dir,"unseen.c",NULL); g_assert_true(g_file_set_contents(unseen,"unseen\n",-1,&e));
    f.name="unseen.c"; g_assert_false(project_write(&p,&f,&e)); g_clear_error(&e);
    data=project_read(&p,"unseen.c",&e); g_assert_nonnull(data); g_free(data);
    g_assert_true(project_write(&p,&f,&e));
    /* Temp fixtures are intentionally retained for inspection, not recursively deleted. */
    g_test_message("Project fixture: %s",dir);
    g_free(unseen); g_free(backup); g_free(path); g_free(secret); project_clear(&p); g_free(dir);
}
static char *child_command(const char *mode) {
#ifdef G_OS_WIN32
    return g_strdup_printf("\"%s\" --child %s",self,mode);
#else
    char *quoted=g_shell_quote(self), *cmd=g_strdup_printf("%s --child %s",quoted,mode); g_free(quoted); return cmd;
#endif
}
static void wait_runner(Runner *r) {
    gint64 deadline=g_get_monotonic_time()+8*G_USEC_PER_SEC;
    while(!runner_poll(r) && g_get_monotonic_time()<deadline) g_usleep(10000);
    g_assert_false(r->active);
}
static void commands(void) {
    GError *e=NULL; Runner r={0}; char *cwd=g_get_current_dir(), *cmd=child_command("emit");
    g_assert_true(runner_start(&r,cwd,cmd,5,&e)); wait_runner(&r);
    g_assert_cmpint(r.exit_code,==,0); g_assert_nonnull(strstr(r.output->str,"stdout marker"));
    g_assert_nonnull(strstr(r.output->str,"stderr marker")); runner_clear(&r); g_free(cmd);
    cmd=child_command("fail"); g_assert_true(runner_start(&r,cwd,cmd,5,&e)); wait_runner(&r);
    g_assert_cmpint(r.exit_code,==,7); runner_clear(&r); g_free(cmd);
    char *saved=g_strdup(g_getenv("OPENAI_API_KEY")); g_setenv("OPENAI_API_KEY","fixture-never-send",TRUE);
    cmd=child_command("env"); g_assert_true(runner_start(&r,cwd,cmd,5,&e)); wait_runner(&r);
    g_assert_cmpint(r.exit_code,==,0); runner_clear(&r); g_free(cmd);
    if(saved) g_setenv("OPENAI_API_KEY",saved,TRUE); else g_unsetenv("OPENAI_API_KEY"); g_free(saved);
    g_free(cwd);
}
static void timeout_cancel(void) {
    GError *e=NULL; Runner r={0}; char *cwd=g_get_current_dir(), *cmd=child_command("hang");
    g_assert_true(runner_start(&r,cwd,cmd,1,&e)); wait_runner(&r);
    g_assert_true(r.timed_out); g_assert_true(r.cancelled); runner_clear(&r);
    g_assert_true(runner_start(&r,cwd,cmd,10,&e)); runner_cancel(&r); wait_runner(&r);
    g_assert_true(r.cancelled); g_assert_false(r.timed_out); runner_clear(&r);
    g_free(cmd); g_free(cwd);
}
static void output_limit(void) {
    Runner r={0}; GError *e=NULL; char *cwd=g_get_current_dir(), *cmd=child_command("large");
    g_assert_true(runner_start(&r,cwd,cmd,5,&e)); wait_runner(&r);
    g_assert_cmpint(r.exit_code,==,0); g_assert_true(r.truncated);
    g_assert_cmpuint(r.output->len,==,256*1024); g_assert_cmpuint(r.output_bytes,>,256*1024);
    runner_clear(&r); g_free(cmd); g_free(cwd);
}
static void working_directory(void) {
    GError *e=NULL; Runner r={0}; char *dir=g_dir_make_tmp("bench project space-XXXXXX",&e);
    g_assert_nonnull(dir); char *cmd=child_command("cwd"), *file=g_build_filename(dir,"cwd-marker.txt",NULL);
    g_assert_true(runner_start(&r,dir,cmd,5,&e)); wait_runner(&r);
    g_assert_cmpint(r.exit_code,==,0); g_assert_true(g_file_test(file,G_FILE_TEST_IS_REGULAR));
    runner_clear(&r); g_remove(file); g_rmdir(dir); g_free(file); g_free(cmd); g_free(dir);
}
static void process_tree(void) {
    GError *e=NULL; Runner r={0}; char *cwd=g_get_current_dir();
    char *dir=g_dir_make_tmp("bench-process-tree-XXXXXX",&e), *marker=g_build_filename(dir,"escaped.txt",NULL);
    char *mode=g_strdup_printf("tree \"%s\"",marker), *cmd=child_command(mode); g_free(mode);
    g_assert_true(runner_start(&r,cwd,cmd,10,&e));
    gint64 deadline=g_get_monotonic_time()+4*G_USEC_PER_SEC;
    while(!strstr(r.output->str,"child launched") && g_get_monotonic_time()<deadline) { runner_poll(&r); g_usleep(10000); }
    g_assert_nonnull(strstr(r.output->str,"child launched"));
    runner_cancel(&r); wait_runner(&r); g_usleep(2500000);
    g_assert_false(g_file_test(marker,G_FILE_TEST_EXISTS)); runner_clear(&r);
    g_rmdir(dir); g_free(dir); g_free(marker); g_free(cmd); g_free(cwd);
}
static void command_plan(void) {
    GError *e=NULL; Plan p={0};
    const char *good="{\"summary\":\"check\",\"steps\":[\"build\"],\"files\":[],\"read_files\":[],\"commands\":[{\"kind\":\"build\",\"command\":\"make\",\"timeout_seconds\":120}]}";
    g_assert_true(parse_plan(good,&p,&e)); g_assert_cmpuint(p.commands->len,==,1); plan_clear(&p);
    const char *bad="{\"summary\":\"check\",\"steps\":[\"build\"],\"files\":[],\"read_files\":[],\"commands\":[{\"kind\":\"build\",\"command\":\"make\",\"timeout_seconds\":0}]}";
    g_assert_false(parse_plan(bad,&p,&e)); g_clear_error(&e);
    g_assert_true(parse_plan("{\"summary\":\"read\",\"steps\":[\"read\"],\"files\":[],\"read_files\":[\"src/main.c\"],\"commands\":[]}",&p,&e)); plan_clear(&p);
}
int main(int argc,char **argv) {
    self=g_canonicalize_filename(argv[0],NULL);
    if(argc>=3 && !strcmp(argv[1],"--child")) {
        if(!strcmp(argv[2],"emit")) { g_print("stdout marker\n"); g_printerr("stderr marker\n"); return 0; }
        if(!strcmp(argv[2],"fail")) return 7;
        if(!strcmp(argv[2],"env")) return g_getenv("OPENAI_API_KEY")?1:0;
        if(!strcmp(argv[2],"cwd")) return g_file_set_contents("cwd-marker.txt","correct directory",-1,NULL)?0:1;
        if(!strcmp(argv[2],"large")) { char buf[8193]; memset(buf,'x',8192); buf[8192]=0; for(int i=0;i<80;i++) g_print("%s",buf); return 0; }
        if(!strcmp(argv[2],"marker") && argc==4) { g_usleep(2000000); return g_file_set_contents(argv[3],"escaped",-1,NULL)?0:1; }
        if(!strcmp(argv[2],"tree") && argc==4) {
            char *args[]={self,"--child","marker",argv[3],NULL};
            if(!g_spawn_async(NULL,args,NULL,0,NULL,NULL,NULL,NULL)) return 1;
            g_print("child launched\n");
        }
        g_usleep(30000000); return 0;
    }
    g_test_init(&argc,&argv,NULL); g_assert_cmpint(curl_global_init(CURL_GLOBAL_DEFAULT),==,CURLE_OK);
    g_test_add_func("/protocol/filenames",filenames); g_test_add_func("/protocol/questions",questions);
    g_test_add_func("/protocol/plans",plans); g_test_add_func("/protocol/envelope",envelope);
    g_test_add_func("/protocol/coding-schema-isolation",coding_schema_isolation);
    g_test_add_func("/protocol/coding-plan-atomicity",coding_plan_atomicity);
    g_test_add_func("/protocol/strict-json",strict_json);
    g_test_add_func("/protocol/strict-coding-json",strict_coding_json);
    g_test_add_func("/protocol/request-escaping",request_json); g_test_add_func("/storage/exclusive-write",disk);
    g_test_add_func("/protocol/limits",limits); g_test_add_func("/api/lifetime",api_lifetime);
    g_test_add_func("/project/paths",paths); g_test_add_func("/project/backup-conflicts",project_edits);
    g_test_add_func("/runner/output-exit-environment",commands); g_test_add_func("/runner/timeout-cancel",timeout_cancel);
    g_test_add_func("/runner/process-tree",process_tree); g_test_add_func("/protocol/command-plan",command_plan);
    g_test_add_func("/runner/output-limit",output_limit);
    g_test_add_func("/runner/working-directory",working_directory);
    int result=g_test_run(); curl_global_cleanup(); g_free(self); return result;
}
