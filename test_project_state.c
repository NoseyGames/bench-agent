#include "project.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>

/* Every write is confined to a fresh temporary fixture. No model, shell command
 * or live project is needed to exercise context and overwrite authority. */
typedef struct { char *dir; Project project; } Fixture;
static void put(Fixture *f,const char *relative,const char *text) {
    char *path=g_build_filename(f->dir,relative,NULL); GError *e=NULL;
    g_assert_true(g_file_set_contents(path,text,-1,&e)); g_assert_no_error(e); g_free(path);
}
static void remove_fixture(const char *path) {
    GDir *dir=g_dir_open(path,0,NULL);
    if(dir) {
        const char *name;
        while((name=g_dir_read_name(dir))) {
            char *child=g_build_filename(path,name,NULL); remove_fixture(child); g_free(child);
        }
        g_dir_close(dir); g_assert_cmpint(g_rmdir(path),==,0);
    } else g_assert_cmpint(g_remove(path),==,0);
}
static void setup(Fixture *f,gconstpointer data) {
    (void)data; GError *e=NULL;
    f->dir=g_dir_make_tmp("bench-project-state-XXXXXX",&e); g_assert_no_error(e); g_assert_nonnull(f->dir);
    g_assert_true(project_open(&f->project,f->dir,&e)); g_assert_no_error(e);
    put(f,"alpha.c","ALPHA_OLD_SENTINEL\n"); put(f,"beta.c","BETA_UNTOUCHED_SENTINEL\n");
}
static void teardown(Fixture *f,gconstpointer data) {
    (void)data; project_clear(&f->project); remove_fixture(f->dir); g_free(f->dir);
}
static char *context(Fixture *f) {
    GError *e=NULL; char *result=project_context_delta(&f->project,&e);
    g_assert_no_error(e); g_assert_nonnull(result); return result;
}
static void initialize(Fixture *f) { g_free(context(f)); }
static Plan replacement(const char *path) {
    Plan plan={0}; plan.count=1; plan.files[0].name=(char *)path; plan.files[0].content="replacement\n"; return plan;
}
static void defaults_and_reads(Fixture *f,gconstpointer data) {
    (void)data; char *text=context(f);
    g_assert_nonnull(strstr(text,"ALPHA_OLD_SENTINEL")); g_assert_nonnull(strstr(text,"BETA_UNTOUCHED_SENTINEL")); g_free(text);
    text=context(f);
    g_assert_nonnull(strstr(text,"alpha.c")); g_assert_null(strstr(text,"ALPHA_OLD_SENTINEL"));
    g_assert_null(strstr(text,"BETA_UNTOUCHED_SENTINEL")); g_assert_cmpuint(g_hash_table_size(f->project.hashes),==,0); g_free(text);
    GError *e=NULL; Plan plan=replacement("beta.c");
    g_assert_false(project_check(&f->project,&plan,&e)); g_assert_nonnull(strstr(e->message,"not read")); g_clear_error(&e);
    char *read=project_read(&f->project,"alpha.c",&e); g_assert_no_error(e); g_free(read);
    text=context(f); g_assert_nonnull(strstr(text,"ALPHA_OLD_SENTINEL")); g_assert_null(strstr(text,"BETA_UNTOUCHED_SENTINEL")); g_free(text);
    g_assert_true(g_hash_table_contains(f->project.hashes,"alpha.c")); g_assert_false(g_hash_table_contains(f->project.hashes,"beta.c"));
    plan=replacement("alpha.c"); g_assert_true(project_check(&f->project,&plan,&e)); g_assert_no_error(e);
}
static void file_deltas(Fixture *f,gconstpointer data) {
    (void)data; initialize(f);
    /* Same byte length: fingerprint comparison must not rely on size/mtime. */
    put(f,"alpha.c","ALPHA_NEW_SENTINEL\n"); put(f,"gamma.c","GAMMA_NEW_SENTINEL\n");
    char *path=g_build_filename(f->dir,"beta.c",NULL); g_assert_cmpint(g_remove(path),==,0); g_free(path);
    char *text=context(f);
    g_assert_nonnull(strstr(text,"CHANGED alpha.c\n")); g_assert_nonnull(strstr(text,"ADDED gamma.c\n"));
    g_assert_nonnull(strstr(text,"DELETED beta.c\n")); g_assert_null(strstr(text,"ALPHA_NEW_SENTINEL"));
    g_assert_null(strstr(text,"GAMMA_NEW_SENTINEL")); g_assert_cmpuint(g_hash_table_size(f->project.hashes),==,0); g_free(text);
    text=context(f); g_assert_nonnull(strstr(text,"(none in observed inventory)")); g_free(text);
}
static void touched_and_stale(Fixture *f,gconstpointer data) {
    (void)data; initialize(f); GError *e=NULL;
    AgentFile file={"alpha.c","AGENT_WRITTEN_SENTINEL\n"};
    g_assert_true(project_write(&f->project,&file,&e)); g_assert_no_error(e);
    char *text=context(f); g_assert_nonnull(strstr(text,"AGENT_WRITTEN_SENTINEL"));
    g_assert_null(strstr(text,"BETA_UNTOUCHED_SENTINEL")); g_free(text);
    Plan plan=replacement("alpha.c"); g_assert_true(project_check(&f->project,&plan,&e));
    put(f,"alpha.c","EXTERNAL_CHANGED_SENTINEL\n");
    g_assert_false(project_check(&f->project,&plan,&e)); g_assert_nonnull(strstr(e->message,"changed since inspection")); g_clear_error(&e);
    text=context(f); g_assert_nonnull(strstr(text,"EXTERNAL_CHANGED_SENTINEL")); g_free(text);
    g_assert_true(project_check(&f->project,&plan,&e)); g_assert_no_error(e);
    char *path=g_build_filename(f->dir,"alpha.c",NULL); g_assert_cmpint(g_remove(path),==,0); g_free(path);
    g_assert_false(project_check(&f->project,&plan,&e)); g_assert_nonnull(strstr(e->message,"removed since inspection")); g_clear_error(&e);
    text=context(f); g_assert_nonnull(strstr(text,"TRACKED FILE MISSING alpha.c"));
    g_assert_false(g_hash_table_contains(f->project.hashes,"alpha.c")); g_free(text);
}
static void persistent_state(Fixture *f,gconstpointer data) {
    (void)data; initialize(f); GError *e=NULL; Project restored={0};
    char *saved=project_state_export(&f->project); g_assert_nonnull(saved);
    g_assert_null(strstr(saved,"ALPHA_OLD_SENTINEL"));
    g_assert_true(project_open(&restored,f->dir,&e)); g_assert_true(project_state_import(&restored,saved,&e)); g_assert_no_error(e);
    g_assert_cmpuint(g_hash_table_size(restored.hashes),==,0); g_assert_true(restored.context_initialized);
    char *text=project_context_delta(&restored,&e); g_assert_no_error(e);
    g_assert_nonnull(strstr(text,"PROJECT UPDATE")); g_assert_null(strstr(text,"ALPHA_OLD_SENTINEL")); g_free(text); g_free(saved);
    char *read=project_read(&f->project,"alpha.c",&e); g_assert_no_error(e); g_free(read);
    saved=project_state_export(&f->project); put(f,"alpha.c","RESTORED_CURRENT_SENTINEL\n");
    g_assert_true(project_state_import(&restored,saved,&e)); g_assert_cmpuint(g_hash_table_size(restored.hashes),==,0);
    Plan plan=replacement("alpha.c"); g_assert_false(project_check(&restored,&plan,&e)); g_clear_error(&e);
    text=project_context_delta(&restored,&e); g_assert_no_error(e);
    g_assert_nonnull(strstr(text,"CHANGED alpha.c\n")); g_assert_nonnull(strstr(text,"RESTORED_CURRENT_SENTINEL"));
    g_assert_null(strstr(text,"BETA_UNTOUCHED_SENTINEL")); g_free(text);
    g_assert_true(project_check(&restored,&plan,&e)); g_assert_no_error(e);
    /* Malformed/path-injected state is rejected without replacing good state. */
    char *before=project_state_export(&restored);
    JsonParser *parser=json_parser_new(); g_assert_true(json_parser_load_from_data(parser,saved,-1,&e));
    JsonObject *o=json_node_get_object(json_parser_get_root(parser));
    JsonArray *tracked=json_object_get_array_member(o,"tracked"); json_array_add_string_element(tracked,"../escape.c");
    JsonGenerator *generator=json_generator_new(); json_generator_set_root(generator,json_parser_get_root(parser));
    char *bad=json_generator_to_data(generator,NULL);
    g_assert_false(project_state_import(&restored,bad,&e)); g_assert_nonnull(e); g_clear_error(&e); g_free(bad);
    char *after=project_state_export(&restored); g_assert_cmpstr(before,==,after); g_free(before); g_free(after);
    json_array_remove_element(tracked,1); json_object_set_string_member(o,"root","C:/different-project");
    bad=json_generator_to_data(generator,NULL); g_assert_false(project_state_import(&restored,bad,&e)); g_clear_error(&e); g_free(bad);
    g_assert_false(project_state_import(&restored,"{\"version\":1}",&e)); g_clear_error(&e);
    g_object_unref(generator); g_object_unref(parser); g_free(saved); project_clear(&restored);
}
static void inventory_bounds(Fixture *f,gconstpointer data) {
    (void)data;
    for(guint i=0;i<131;i++) { char *name=g_strdup_printf("f%03u.c",i); put(f,name,"small\n"); g_free(name); }
    initialize(f); g_assert_true(f->project.inventory_truncated); g_assert_cmpuint(g_hash_table_size(f->project.inventory),==,128);
    /* Inserting an early name displaces a live file from the bounded inventory. */
    put(f,"a.c","new\n"); char *text=context(f);
    g_assert_nonnull(strstr(text,"NEWLY OBSERVED a.c")); g_assert_null(strstr(text,"DELETED ")); g_free(text);
    for(guint i=0;i<40;i++) {
        char *name=g_strdup_printf("f%03u.c",i); g_assert_true(project_track_file(&f->project,name,NULL)); g_free(name);
    }
    g_assert_cmpuint(f->project.tracked->len,==,32);
    char *saved=project_state_export(&f->project); g_assert_nonnull(saved); g_assert_cmpuint(strlen(saved),<,128*1024); g_free(saved);
    g_assert_false(g_hash_table_contains(f->project.hashes,"f039.c"));
    GError *e=NULL; g_assert_false(project_track_file(&f->project,"../escape.c",&e)); g_assert_nonnull(e); g_clear_error(&e);
}
static void content_budget(Fixture *f,gconstpointer data) {
    (void)data; initialize(f);
    char *large=g_strnfill(96*1024-512,'x');
    const char *names[]={"one.c","two.c","three.c"};
    for(guint i=0;i<G_N_ELEMENTS(names);i++) { put(f,names[i],large); g_assert_true(project_track_file(&f->project,names[i],NULL)); }
    g_free(large); char *text=context(f);
    g_assert_nonnull(strstr(text,"TRACKED FILE OMITTED (192 KiB limit; request again before editing) one.c"));
    g_assert_false(g_hash_table_contains(f->project.hashes,"one.c"));
    g_assert_true(g_hash_table_contains(f->project.hashes,"two.c")); g_assert_true(g_hash_table_contains(f->project.hashes,"three.c"));
    g_assert_cmpuint(strlen(text),<,220*1024); g_free(text);
    /* Requesting an omitted file makes it recent enough to receive contents. */
    g_assert_true(project_track_file(&f->project,"one.c",NULL)); text=context(f);
    g_assert_true(g_hash_table_contains(f->project.hashes,"one.c")); g_assert_false(g_hash_table_contains(f->project.hashes,"two.c")); g_free(text);
}
int main(int argc,char **argv) {
    g_test_init(&argc,&argv,NULL);
    g_test_add("/project-state/defaults-and-reads",Fixture,NULL,setup,defaults_and_reads,teardown);
    g_test_add("/project-state/file-deltas",Fixture,NULL,setup,file_deltas,teardown);
    g_test_add("/project-state/touched-and-stale",Fixture,NULL,setup,touched_and_stale,teardown);
    g_test_add("/project-state/persistent-state",Fixture,NULL,setup,persistent_state,teardown);
    g_test_add("/project-state/inventory-bounds",Fixture,NULL,setup,inventory_bounds,teardown);
    g_test_add("/project-state/content-budget",Fixture,NULL,setup,content_budget,teardown);
    return g_test_run();
}
