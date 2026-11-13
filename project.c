#include "project.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <errno.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
#define CONTEXT_LIMIT (192*1024)
static gboolean error(GError **e,const char *message,const char *path) {
    g_set_error(e,G_IO_ERROR,G_IO_ERROR_FAILED,"%s: %s",message,path); return FALSE;
}
static gboolean private_name(const char *name) {
    char *s=g_ascii_strdown(name,-1);
    gboolean result=!strcmp(s,".git") || !strcmp(s,".bench-agent") || !strcmp(s,".ssh") || !strcmp(s,".aws") ||
        g_str_has_prefix(s,".env") || g_str_has_suffix(s,".pem") || g_str_has_suffix(s,".key") ||
        g_str_has_suffix(s,".pfx") || g_str_has_suffix(s,".p12") || !strcmp(s,".npmrc") ||
        !strcmp(s,".pypirc") || !strcmp(s,".netrc") || strstr(s,"credentials") || strstr(s,"secrets");
    g_free(s); return result;
}
gboolean project_path_valid(const char *s) {
    if(!s || !*s || strlen(s)>500 || !g_utf8_validate(s,-1,NULL) || s[0]=='/' || strchr(s,'\\') || strchr(s,':')) return FALSE;
    char **parts=g_strsplit(s,"/",-1); gboolean ok=TRUE;
    for(guint i=0;parts[i];i++) {
        const char *v=parts[i]; gsize len=strlen(v);
        if(!len || !strcmp(v,".") || !strcmp(v,"..") || len>150 || v[len-1]=='.' || v[len-1]==' ' || private_name(v)) { ok=FALSE; break; }
        for(const char *c=v;*c;c++) if((unsigned char)*c<32 || strchr("<>\"|?*",*c)) { ok=FALSE; break; }
        char *base=g_ascii_strup(v,-1), *dot=strchr(base,'.'); if(dot) *dot=0;
        if(!strcmp(base,"CON") || !strcmp(base,"NUL") || !strcmp(base,"PRN") || !strcmp(base,"AUX") ||
            ((!strncmp(base,"COM",3) || !strncmp(base,"LPT",3)) && strlen(base)==4 && base[3]>='1' && base[3]<='9')) ok=FALSE;
        g_free(base); if(!ok) break;
    }
    g_strfreev(parts); return ok;
}
static gboolean link_path(const char *path) {
#ifdef G_OS_WIN32
    gunichar2 *wide=g_utf8_to_utf16(path,-1,NULL,NULL,NULL);
    if(!wide) return TRUE;
    DWORD a=GetFileAttributesW((wchar_t *)wide); g_free(wide);
    return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return g_file_test(path,G_FILE_TEST_IS_SYMLINK);
#endif
}
/* Validate every existing component, including the final file. This protects
 * against accidental symlink escapes; concurrent hostile filesystem mutation
 * by another local process is outside the app's security boundary. */
static char *resolve(Project *p,const char *relative,GError **e) {
    if(!project_path_valid(relative)) { error(e,"Rejected relative path",relative); return NULL; }
    char **parts=g_strsplit(relative,"/",-1); char *path=g_strdup(p->root);
    for(guint i=0;parts[i];i++) {
        char *next=g_build_filename(path,parts[i],NULL); g_free(path); path=next;
        if(link_path(path) || (parts[i+1] && g_file_test(path,G_FILE_TEST_EXISTS) && !g_file_test(path,G_FILE_TEST_IS_DIR))) {
            error(e,"Symlink/reparse point or invalid parent",relative); g_clear_pointer(&path,g_free); break;
        }
    }
    g_strfreev(parts); return path;
}
void project_clear(Project *p) {
    g_free(p->root); g_free(p->run_dir); if(p->hashes) g_hash_table_unref(p->hashes); memset(p,0,sizeof *p);
}
gboolean project_open(Project *p,const char *root,GError **e) {
    project_clear(p); char *path=g_canonicalize_filename(root,NULL);
    if(!g_file_test(path,G_FILE_TEST_IS_DIR) || link_path(path)) { error(e,"Select a real project directory",path); g_free(path); return FALSE; }
    p->root=path; p->hashes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); return TRUE;
}
static gboolean read_text(const char *path,char **out,GError **e) {
    GStatBuf statbuf;
    if(g_stat(path,&statbuf)!=0 || !S_ISREG(statbuf.st_mode) || statbuf.st_size>MAX_FILE_BYTES) return error(e,"Not a regular text file or over 128 KiB",path);
    gsize n=0;
    if(!g_file_get_contents(path,out,&n,e)) return FALSE;
    if(n>MAX_FILE_BYTES || memchr(*out,0,n) || !g_utf8_validate(*out,n,NULL)) {
        g_clear_pointer(out,g_free); return error(e,"Not UTF-8 text",path);
    }
    return TRUE;
}
char *project_read(Project *p,const char *relative,GError **e) {
    char *path=resolve(p,relative,e), *text=NULL; if(!path) return NULL;
    gboolean ok=read_text(path,&text,e); g_free(path); if(!ok) return NULL;
    char *hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
    g_hash_table_replace(p->hashes,g_strdup(relative),hash); return text;
}
static gboolean skip_dir(const char *s) {
    const char *names[]={"node_modules","vendor","target","dist","build",".venv","venv","__pycache__",".next",".cache",".idea",".vs",NULL};
    for(guint i=0;names[i];i++) if(!g_ascii_strcasecmp(names[i],s)) return TRUE;
    return private_name(s);
}
static gint compare(gconstpointer a,gconstpointer b) { return g_strcmp0(*(char *const *)a,*(char *const *)b); }
static void scan(Project *p,const char *relative,guint depth,GString *manifest,GString *contents,guint *visited) {
    if(depth>12 || *visited>=3000) { p->skipped++; return; }
    char *full=g_build_filename(p->root,relative,NULL); GDir *dir=g_dir_open(full,0,NULL); g_free(full);
    if(!dir) { p->skipped++; return; }
    GPtrArray *names=g_ptr_array_new_with_free_func(g_free); const char *name;
    while((name=g_dir_read_name(dir)) && names->len<3000) g_ptr_array_add(names,g_strdup(name));
    g_dir_close(dir); g_ptr_array_sort(names,compare);
    /* Root files precede subdirectories, keeping manifests/config in context. */
    for(int pass=0;pass<2;pass++) for(guint i=0;i<names->len && *visited<3000;i++) {
        name=g_ptr_array_index(names,i);
        char *rel=*relative?g_strconcat(relative,"/",name,NULL):g_strdup(name);
        full=g_build_filename(p->root,rel,NULL);
        gboolean isdir=g_file_test(full,G_FILE_TEST_IS_DIR);
        if((pass==0 && isdir) || (pass==1 && !isdir)) { g_free(full); g_free(rel); continue; }
        (*visited)++;
        if(!project_path_valid(rel) || link_path(full) || (isdir && skip_dir(name))) p->skipped++;
        else if(isdir) scan(p,rel,depth+1,manifest,contents,visited);
        else {
            if(manifest->len<48*1024) g_string_append_printf(manifest,"%s\n",rel);
            char *text=NULL; GError *e=NULL;
            if(contents->len<CONTEXT_LIMIT && p->read_count<128 && read_text(full,&text,&e) && contents->len+strlen(text)+strlen(rel)+32<CONTEXT_LIMIT) {
                g_string_append_printf(contents,"\n--- FILE %s ---\n%s\n--- END FILE ---\n",rel,text);
                g_hash_table_replace(p->hashes,g_strdup(rel),g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1)); p->read_count++;
            } else p->skipped++;
            g_free(text); g_clear_error(&e);
        }
        g_free(full); g_free(rel);
    }
    g_ptr_array_unref(names);
}
char *project_context(Project *p,GError **e) {
    if(!p->root) { error(e,"No project selected",""); return NULL; }
    g_hash_table_remove_all(p->hashes); p->read_count=p->skipped=0;
    GString *manifest=g_string_new("PROJECT FILE INVENTORY (bounded; excluded directories/secrets omitted):\n"), *contents=g_string_new(NULL);
    guint visited=0; scan(p,"",0,manifest,contents,&visited);
    g_string_append_printf(manifest,"\nLoaded %u text files. Skipped/limited %u entries. Do not overwrite unseen files; request them using read_files first.\n",p->read_count,p->skipped);
    g_string_append_len(manifest,contents->str,contents->len); g_string_free(contents,TRUE); return g_string_free(manifest,FALSE);
}
static gboolean check_file(Project *p,const AgentFile *file,GError **e) {
    char *path=resolve(p,file->name,e); if(!path) return FALSE;
    const char *expected=g_hash_table_lookup(p->hashes,file->name); gboolean ok=TRUE;
    if(g_file_test(path,G_FILE_TEST_EXISTS)) {
        char *content=NULL;
        if(!expected) ok=error(e,"Existing file was not read; request it before editing",file->name);
        else if(!(ok=read_text(path,&content,e))) {}
        else {
            char *hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,content,-1);
            if(strcmp(hash,expected)) ok=error(e,"File changed since inspection; start a new task",file->name);
            g_free(hash); g_free(content);
        }
    } else if(expected) ok=error(e,"File removed since inspection",file->name);
    g_free(path); return ok;
}
gboolean project_check(Project *p,const Plan *plan,GError **e) {
    for(guint i=0;i<plan->count;i++) if(!check_file(p,&plan->files[i],e)) return FALSE;
    return TRUE;
}
static gboolean ensure_run(Project *p,GError **e) {
    if(p->run_dir) return TRUE;
    char *base=g_build_filename(p->root,".bench-agent",NULL);
    if(link_path(base) || g_mkdir_with_parents(base,0700)!=0) { error(e,"Cannot create backup directory",base); g_free(base); return FALSE; }
    char *run=g_build_filename(base,"run-XXXXXX",NULL); g_free(base);
    if(!g_mkdtemp(run)) { error(e,"Cannot create run directory",run); g_free(run); return FALSE; }
    p->run_dir=run; return TRUE;
}
gboolean project_log(Project *p,const char *text,GError **e) {
    if(!ensure_run(p,e)) return FALSE;
    char *path=g_build_filename(p->run_dir,"session.log",NULL); FILE *f=g_fopen(path,"ab");
    if(!f) { error(e,"Cannot open session log",path); g_free(path); return FALSE; }
    gsize n=strlen(text); gboolean ok=fwrite(text,1,n,f)==n;
    if(fclose(f)!=0) ok=FALSE;
    if(!ok) error(e,"Cannot persist session log",path);
    g_free(path); return ok;
}
gboolean project_write(Project *p,const AgentFile *file,GError **e) {
    if(!check_file(p,file,e) || !ensure_run(p,e)) return FALSE;
    char *path=resolve(p,file->name,e); if(!path) return FALSE;
    gboolean exists=g_file_test(path,G_FILE_TEST_EXISTS), ok=TRUE;
    if(exists) {
        char *backup=g_build_filename(p->run_dir,"original",file->name,NULL);
        if(!g_file_test(backup,G_FILE_TEST_EXISTS)) {
            char *parent=g_path_get_dirname(backup);
            if(g_mkdir_with_parents(parent,0700)!=0) ok=error(e,"Cannot create backup parent",parent);
            g_free(parent);
            if(ok) {
                GFile *src=g_file_new_for_path(path), *dst=g_file_new_for_path(backup);
                ok=g_file_copy(src,dst,G_FILE_COPY_NONE,NULL,NULL,NULL,e); g_object_unref(src); g_object_unref(dst);
            }
        }
        g_free(backup);
    }
    char *parent=g_path_get_dirname(path);
    if(ok && g_mkdir_with_parents(parent,0755)!=0) ok=error(e,"Cannot create project subdirectory",parent);
    g_free(parent);
    char *entry=g_strdup_printf("%s %s\n",exists?"REPLACE (backup in original/)":"CREATE",file->name);
    if(ok) ok=project_log(p,entry,e);
    g_free(entry);
    if(ok) {
        GFile *f=g_file_new_for_path(path);
        /* Replace via temporary file and rename; preserve existing mode on Unix. */
        if(exists) ok=g_file_replace_contents(f,file->content,strlen(file->content),NULL,FALSE,G_FILE_CREATE_NONE,NULL,NULL,e);
        else {
            GFileOutputStream *stream=g_file_create(f,G_FILE_CREATE_NONE,NULL,e);
            ok=stream!=NULL;
            if(stream) {
                ok=g_output_stream_write_all(G_OUTPUT_STREAM(stream),file->content,strlen(file->content),NULL,NULL,e);
                if(ok) ok=g_output_stream_close(G_OUTPUT_STREAM(stream),NULL,e);
                else g_output_stream_close(G_OUTPUT_STREAM(stream),NULL,NULL);
                g_object_unref(stream);
            }
        }
        g_object_unref(f);
    }
    if(ok) g_hash_table_replace(p->hashes,g_strdup(file->name),g_compute_checksum_for_string(G_CHECKSUM_SHA256,file->content,-1));
    g_free(path); return ok;
}
