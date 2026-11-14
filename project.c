#include "project.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <errno.h>
#include <json-glib/json-glib.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
#define CONTEXT_LIMIT (192*1024)
#define INVENTORY_LIMIT 128
#define TRACKED_LIMIT 32
#define STATE_LIMIT (128*1024)
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
    g_free(p->root); g_free(p->run_dir);
    if(p->hashes) g_hash_table_unref(p->hashes);
    if(p->inventory) g_hash_table_unref(p->inventory);
    if(p->tracked) g_ptr_array_unref(p->tracked);
    memset(p,0,sizeof *p);
}
gboolean project_open(Project *p,const char *root,GError **e) {
    project_clear(p); char *path=g_canonicalize_filename(root,NULL);
    if(!g_file_test(path,G_FILE_TEST_IS_DIR) || link_path(path)) { error(e,"Select a real project directory",path); g_free(path); return FALSE; }
    p->root=path; p->hashes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    p->inventory=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    p->tracked=g_ptr_array_new_with_free_func(g_free); return TRUE;
}
gboolean project_track_file(Project *p,const char *relative,GError **e) {
    if(!p->root || !project_path_valid(relative)) return error(e,"Rejected tracked project path",relative?relative:"");
    for(guint i=0;i<p->tracked->len;i++) if(!strcmp(g_ptr_array_index(p->tracked,i),relative)) {
        /* Recent paths survive eviction; this operation grants no read hash. */
        g_ptr_array_remove_index(p->tracked,i); break;
    }
    if(p->tracked->len==TRACKED_LIMIT) g_ptr_array_remove_index(p->tracked,0);
    g_ptr_array_add(p->tracked,g_strdup(relative)); return TRUE;
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
    g_hash_table_replace(p->hashes,g_strdup(relative),hash);
    project_track_file(p,relative,NULL); return text;
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
/* Inventory scans deliberately do not call project_read or update hashes.
 * Small files get byte hashes, so same-size edits within one timestamp tick
 * are still visible. Larger, non-editable files use metadata fingerprints. */
static char *inventory_fingerprint(const char *path,const GStatBuf *st) {
    char *bytes=NULL; gsize len=0;
    if(st->st_size<=MAX_FILE_BYTES && g_file_get_contents(path,&bytes,&len,NULL)) {
        if(len<=MAX_FILE_BYTES) {
            char *hash=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)bytes,len);
            g_free(bytes); return hash;
        }
        g_free(bytes);
    }
    return g_strdup_printf("m:%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
        (gint64)st->st_size,(gint64)st->st_mtime,(gint64)st->st_ctime);
}
static void inventory_scan(Project *p,const char *relative,guint depth,GHashTable *out,guint *visited,gboolean *truncated) {
    if(depth>12 || *visited>=3000) { *truncated=TRUE; return; }
    char *full=g_build_filename(p->root,relative,NULL); GDir *dir=g_dir_open(full,0,NULL); g_free(full);
    if(!dir) { *truncated=TRUE; return; }
    GPtrArray *names=g_ptr_array_new_with_free_func(g_free); const char *name;
    while((name=g_dir_read_name(dir))) {
        if(names->len==3000) { *truncated=TRUE; break; }
        g_ptr_array_add(names,g_strdup(name));
    }
    g_dir_close(dir); g_ptr_array_sort(names,compare);
    for(int pass=0;pass<2;pass++) for(guint i=0;i<names->len;i++) {
        if(*visited>=3000) { *truncated=TRUE; goto done; }
        name=g_ptr_array_index(names,i);
        char *rel=*relative?g_strconcat(relative,"/",name,NULL):g_strdup(name);
        full=g_build_filename(p->root,rel,NULL);
        gboolean isdir=g_file_test(full,G_FILE_TEST_IS_DIR);
        if((pass==0 && isdir) || (pass==1 && !isdir)) { g_free(full); g_free(rel); continue; }
        (*visited)++;
        if(project_path_valid(rel) && !link_path(full) && !(isdir && skip_dir(name))) {
            if(isdir) inventory_scan(p,rel,depth+1,out,visited,truncated);
            else {
                GStatBuf st;
                if(g_stat(full,&st)==0 && S_ISREG(st.st_mode)) {
                    if(g_hash_table_size(out)==INVENTORY_LIMIT) {
                        *truncated=TRUE; g_free(full); g_free(rel); goto done;
                    }
                    g_hash_table_insert(out,g_strdup(rel),inventory_fingerprint(full,&st));
                }
            }
        }
        g_free(full); g_free(rel);
    }
done:
    g_ptr_array_unref(names);
}
static GPtrArray *sorted_keys(GHashTable *table) {
    GPtrArray *keys=g_ptr_array_new(); GHashTableIter it; gpointer key;
    g_hash_table_iter_init(&it,table);
    while(g_hash_table_iter_next(&it,&key,NULL)) g_ptr_array_add(keys,key);
    g_ptr_array_sort(keys,compare); return keys;
}
static gboolean append_bounded_line(GString *s,gsize start,gsize limit,const char *kind,const char *path) {
    if(s->len-start+strlen(kind)+strlen(path)+2>limit) return FALSE;
    g_string_append_printf(s,"%s%s\n",kind,path); return TRUE;
}
char *project_context_delta(Project *p,GError **e) {
    if(!p->root) { error(e,"No project selected",""); return NULL; }
    GHashTable *next=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    guint visited=0; gboolean truncated=FALSE;
    inventory_scan(p,"",0,next,&visited,&truncated);
    if(!p->context_initialized) {
        char *initial=project_context(p,e);
        if(!initial) { g_hash_table_unref(next); return NULL; }
        g_hash_table_unref(p->inventory); p->inventory=next;
        p->inventory_truncated=truncated; p->context_initialized=TRUE;
        return initial;
    }
    GString *out=g_string_new("PROJECT UPDATE (cached state; inventory is not file content):\n");
    GPtrArray *keys=sorted_keys(next); gsize start=out->len;
    g_string_append(out,"Inventory:\n");
    for(guint i=0;i<keys->len;i++) if(!append_bounded_line(out,start,8192,"",g_ptr_array_index(keys,i))) {
        g_string_append(out,"[inventory display limited]\n"); break;
    }
    if(truncated) g_string_append(out,"[inventory scan limited to 128 files / 3000 entries / depth 12; excluded directories/secrets omitted]\n");
    g_string_append(out,"Changes since last observation:\n"); start=out->len;
    gboolean changed=FALSE, delta_limited=FALSE;
    for(guint i=0;i<keys->len;i++) {
        const char *path=g_ptr_array_index(keys,i), *old=g_hash_table_lookup(p->inventory,path);
        const char *current=g_hash_table_lookup(next,path), *kind=NULL;
        if(!old) kind=p->inventory_truncated?"NEWLY OBSERVED ":"ADDED ";
        else if(strcmp(old,current)) kind="CHANGED ";
        if(kind) { changed=TRUE; if(!append_bounded_line(out,start,16384,kind,path)) { delta_limited=TRUE; break; } }
    }
    g_ptr_array_unref(keys); keys=sorted_keys(p->inventory);
    for(guint i=0;i<keys->len && !delta_limited;i++) {
        const char *path=g_ptr_array_index(keys,i);
        if(g_hash_table_contains(next,path)) continue;
        /* A cap shift does not mean deletion. Recheck old paths before reporting
         * them missing, without reading content or granting write permission. */
        char *full=resolve(p,path,NULL); GStatBuf st; const char *kind=NULL;
        if(!full) kind="UNAVAILABLE ";
        else if(g_stat(full,&st)!=0 && (errno==ENOENT || errno==ENOTDIR)) kind="DELETED ";
        else if(!g_file_test(full,G_FILE_TEST_IS_REGULAR)) kind="UNAVAILABLE ";
        g_free(full);
        if(kind) { changed=TRUE; if(!append_bounded_line(out,start,16384,kind,path)) delta_limited=TRUE; }
    }
    g_ptr_array_unref(keys);
    if(delta_limited) g_string_append(out,"[change display limited]\n");
    if(!changed) g_string_append(out,"(none in observed inventory)\n");
    g_string_append(out,"Only file contents below are current. Request other existing files with read_files before editing.\n");
    GHashTable *read_hashes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    gsize content_bytes=0; guint read_count=0;
    /* Most recently requested/touched files get the finite content budget.
     * Omitted paths remain tracked but receive no write hash this turn. */
    for(guint i=p->tracked->len;i>0;i--) {
        const char *relative=g_ptr_array_index(p->tracked,i-1); char *path=resolve(p,relative,e), *content=NULL;
        if(!path) goto fail;
        GStatBuf st;
        if(g_stat(path,&st)!=0 && (errno==ENOENT || errno==ENOTDIR)) {
            g_string_append_printf(out,"TRACKED FILE MISSING %s\n",relative); g_free(path); continue;
        }
        gboolean ok=read_text(path,&content,e); g_free(path); if(!ok) goto fail;
        gsize bytes=strlen(content)+strlen(relative)+40;
        if(content_bytes+bytes>CONTEXT_LIMIT) {
            g_string_append_printf(out,"TRACKED FILE OMITTED (192 KiB limit; request again before editing) %s\n",relative);
            g_free(content); continue;
        }
        g_string_append_printf(out,"\n--- FILE %s ---\n%s\n--- END FILE ---\n",relative,content);
        g_hash_table_insert(read_hashes,g_strdup(relative),g_compute_checksum_for_string(G_CHECKSUM_SHA256,content,-1));
        g_free(content); content_bytes+=bytes; read_count++;
    }
    g_hash_table_unref(p->hashes); p->hashes=read_hashes; p->read_count=read_count;
    g_hash_table_unref(p->inventory); p->inventory=next; p->inventory_truncated=truncated;
    return g_string_free(out,FALSE);
fail:
    g_hash_table_unref(read_hashes); g_hash_table_unref(next); g_string_free(out,TRUE); return NULL;
}
char *project_state_export(Project *p) {
    if(!p->root) return NULL;
    JsonBuilder *b=json_builder_new(); json_builder_begin_object(b);
    json_builder_set_member_name(b,"version"); json_builder_add_int_value(b,1);
    json_builder_set_member_name(b,"root"); json_builder_add_string_value(b,p->root);
    json_builder_set_member_name(b,"initialized"); json_builder_add_boolean_value(b,p->context_initialized);
    json_builder_set_member_name(b,"tracked"); json_builder_begin_array(b);
    for(guint i=0;i<p->tracked->len;i++) json_builder_add_string_value(b,g_ptr_array_index(p->tracked,i));
    json_builder_end_array(b);
    json_builder_set_member_name(b,"inventory"); json_builder_begin_array(b);
    GPtrArray *keys=sorted_keys(p->inventory);
    for(guint i=0;i<keys->len;i++) {
        const char *key=g_ptr_array_index(keys,i); json_builder_begin_object(b);
        json_builder_set_member_name(b,"path"); json_builder_add_string_value(b,key);
        json_builder_set_member_name(b,"fingerprint"); json_builder_add_string_value(b,g_hash_table_lookup(p->inventory,key));
        json_builder_end_object(b);
    }
    g_ptr_array_unref(keys); json_builder_end_array(b);
    json_builder_set_member_name(b,"inventory_truncated"); json_builder_add_boolean_value(b,p->inventory_truncated);
    json_builder_end_object(b); JsonNode *root=json_builder_get_root(b); JsonGenerator *g=json_generator_new();
    json_generator_set_root(g,root); gsize length=0; char *text=json_generator_to_data(g,&length);
    json_node_free(root); g_object_unref(g); g_object_unref(b);
    /* In practice the field bounds stay below this. Avoid producing an import
     * the host cannot safely cache if an unusually long root tips the limit. */
    if(length>STATE_LIMIT) g_clear_pointer(&text,g_free);
    return text;
}
static gboolean state_value(JsonObject *o,const char *name,GType type) {
    JsonNode *v=json_object_get_member(o,name);
    return v && JSON_NODE_HOLDS_VALUE(v) && json_node_get_value_type(v)==type;
}
static gboolean fingerprint_valid(const char *s) {
    if(!s) return FALSE;
    if(strlen(s)==64) {
        gboolean hex=TRUE;
        for(const char *c=s;*c;c++) if(!g_ascii_isxdigit(*c)) { hex=FALSE; break; }
        if(hex) return TRUE;
    }
    if(!g_str_has_prefix(s,"m:") || strlen(s)>128) return FALSE;
    char **fields=g_strsplit(s+2,":",-1); gboolean ok=g_strv_length(fields)==3;
    for(guint i=0;ok && fields[i];i++) {
        const char *c=fields[i]; if(*c=='-') c++;
        if(!*c) ok=FALSE;
        for(;*c;c++) if(!g_ascii_isdigit(*c)) ok=FALSE;
    }
    g_strfreev(fields); return ok;
}
gboolean project_state_import(Project *p,const char *json,GError **e) {
    if(!p->root || !json || strlen(json)>STATE_LIMIT) return error(e,"Invalid project state size or root","");
    JsonParser *parser=json_parser_new();
    if(!json_parser_load_from_data(parser,json,-1,e)) { g_object_unref(parser); return FALSE; }
    JsonNode *root=json_parser_get_root(parser); GHashTable *inventory=NULL; GPtrArray *tracked=NULL;
    gboolean ok=FALSE;
    if(!root || !JSON_NODE_HOLDS_OBJECT(root)) goto done;
    JsonObject *o=json_node_get_object(root);
    if(json_object_get_size(o)!=6 || !state_value(o,"version",G_TYPE_INT64) || json_object_get_int_member(o,"version")!=1 ||
       !state_value(o,"root",G_TYPE_STRING) || strcmp(json_object_get_string_member(o,"root"),p->root) ||
       !state_value(o,"initialized",G_TYPE_BOOLEAN) || !state_value(o,"inventory_truncated",G_TYPE_BOOLEAN)) goto done;
    JsonNode *tn=json_object_get_member(o,"tracked"), *in=json_object_get_member(o,"inventory");
    if(!tn || !in || !JSON_NODE_HOLDS_ARRAY(tn) || !JSON_NODE_HOLDS_ARRAY(in)) goto done;
    JsonArray *ta=json_node_get_array(tn), *ia=json_node_get_array(in);
    if(json_array_get_length(ta)>TRACKED_LIMIT || json_array_get_length(ia)>INVENTORY_LIMIT) goto done;
    tracked=g_ptr_array_new_with_free_func(g_free); inventory=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    for(guint i=0;i<json_array_get_length(ta);i++) {
        JsonNode *n=json_array_get_element(ta,i);
        if(!JSON_NODE_HOLDS_VALUE(n) || json_node_get_value_type(n)!=G_TYPE_STRING) goto done;
        const char *path=json_node_get_string(n); if(!project_path_valid(path)) goto done;
        for(guint j=0;j<tracked->len;j++) if(!strcmp(path,g_ptr_array_index(tracked,j))) goto done;
        g_ptr_array_add(tracked,g_strdup(path));
    }
    for(guint i=0;i<json_array_get_length(ia);i++) {
        JsonNode *n=json_array_get_element(ia,i); if(!JSON_NODE_HOLDS_OBJECT(n)) goto done;
        JsonObject *entry=json_node_get_object(n);
        if(json_object_get_size(entry)!=2 || !state_value(entry,"path",G_TYPE_STRING) || !state_value(entry,"fingerprint",G_TYPE_STRING)) goto done;
        const char *path=json_object_get_string_member(entry,"path"), *hash=json_object_get_string_member(entry,"fingerprint");
        if(!project_path_valid(path) || !fingerprint_valid(hash) || g_hash_table_contains(inventory,path)) goto done;
        g_hash_table_insert(inventory,g_strdup(path),g_strdup(hash));
    }
    /* Imported fingerprints are observations, never proof the model has read a
     * file in this execution. Only the following context/read can grant that. */
    g_hash_table_remove_all(p->hashes);
    g_ptr_array_unref(p->tracked); p->tracked=tracked; tracked=NULL;
    g_hash_table_unref(p->inventory); p->inventory=inventory; inventory=NULL;
    p->context_initialized=json_object_get_boolean_member(o,"initialized");
    p->inventory_truncated=json_object_get_boolean_member(o,"inventory_truncated"); ok=TRUE;
done:
    if(tracked) g_ptr_array_unref(tracked);
    if(inventory) g_hash_table_unref(inventory);
    g_object_unref(parser);
    return ok?TRUE:error(e,"Invalid or mismatched project state","");
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
    if(ok) {
        g_hash_table_replace(p->hashes,g_strdup(file->name),g_compute_checksum_for_string(G_CHECKSUM_SHA256,file->content,-1));
        project_track_file(p,file->name,NULL);
    }
    g_free(path); return ok;
}
