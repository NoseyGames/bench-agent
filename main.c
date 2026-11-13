/* Bench / visual agent. C11, GTK 3, curl, JSON-GLib.
 * All GTK and agent state belongs to the main thread. curl's multi API is
 * polled by the animation timer; no workers can outlive a destroyed window.
 * The visual cursor operates only this simulated workspace, never the OS. */
#include "agent.h"
#include "project.h"
#include "runner.h"
#include "cursor.h"
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

typedef enum { TASK_INPUT, CLARIFICATION, EXECUTION, REPORT } Stage;
typedef enum { OPEN_FILE, HOLD_OPEN, MOVE_EDITOR, TYPE_FILE, REVIEW_FILE, MOVE_SAVE, SAVE_FILE, MOVE_RUN, RUN_COMMAND, WAIT_COMMAND } Motion;
typedef struct {
    GtkWidget *window, *chat, *input, *send, *cancel, *status, *model;
    GtkWidget *editor, *overlay, *cursor, *files, *tabs, *save, *folder, *speed;
    GtkWidget *project_entry, *browse, *terminal, *run;
    GtkWidget *chat_scroll, *workspace, *notebook, *workspace_toggle, *pause, *new_chat;
    GtkWidget *history, *heading, *project_name, *settings, *progress, *pace_label;
    GtkWidget *change_select, *before, *after, *placeholder;
    GString *transcript;
    char *api_key, *originals[MAX_FILES];
    gboolean paused, readable_smoke, following, pause_probed;
    gsize probe_offset;
    double probe_x, probe_y;
    gint64 execution_started;
    double hold;
    GtkWidget *file_buttons[MAX_FILES];
    Stage stage;
    Motion motion;
    gboolean pending, planning, demo, smoke, smoke_repair, failed, stopping, build_ok, test_ok;
    guint timer, index, saved, dwell;
    guint command_index, rounds, read_rounds, commands_passed;
    guint64 displayed_bytes;
    gboolean captured_cursor;
    gsize offset;
    double x, y, tx, ty, pulse, budget;
    gint64 previous;
    char *task, *question_text, *directory, *context, *answers;
    GString *feedback;
    Project project;
    Runner runner;
    GdkPixbuf *cursor_image;
    int hot_x, hot_y;
    Questions questions;
    Plan plan;
    Api api;
    int exit_code;
} App;
static void request(App *a,gboolean planning,const char *answers);
static void next_round(App *a,const char *reason);
static void commands_complete(App *a);
static GtkWidget *text_view(gboolean editable,gboolean wrap);
static GtkWidget *scroll(GtkWidget *child);
static GtkWidget *label(const char *s);
static void show_changes(GtkButton *b,gpointer data);
static void archive_chat(App *a);
static void toggle_pause(GtkButton *b,gpointer data);
static void project_selected(GtkEditable *entry,gpointer data);

static void empty_box(GtkWidget *box) {
    GList *children=gtk_container_get_children(GTK_CONTAINER(box));
    for(GList *i=children;i;i=i->next) gtk_widget_destroy(i->data);
    g_list_free(children);
}
static gboolean follow_chat(gpointer data) {
    App *a=data; GtkAdjustment *adj=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(a->chat_scroll));
    if(a->following) gtk_adjustment_set_value(adj,gtk_adjustment_get_upper(adj)-gtk_adjustment_get_page_size(adj));
    return G_SOURCE_REMOVE;
}
static gboolean chat_scrolled(GtkWidget *w,GdkEventScroll *e,gpointer data) {
    (void)w; App *a=data;
    if(e->direction==GDK_SCROLL_UP || (e->direction==GDK_SCROLL_SMOOTH && e->delta_y<0)) a->following=FALSE;
    return FALSE;
}
static gboolean scroll_dragged(GtkWidget *w,GdkEventButton *e,gpointer data) {
    (void)w; (void)e; ((App *)data)->following=FALSE; return FALSE;
}
static void chat_position(GtkAdjustment *adj,gpointer data) {
    App *a=data;
    if(gtk_adjustment_get_value(adj)+gtk_adjustment_get_page_size(adj)>=gtk_adjustment_get_upper(adj)-30) a->following=TRUE;
}
static void chat_extent(GtkAdjustment *adj,gpointer data) {
    (void)adj; App *a=data;
    if(a->following) follow_chat(a);
}
static void composer_changed(GtkTextBuffer *buffer,gpointer data) {
    App *a=data; gtk_widget_set_visible(a->placeholder,gtk_text_buffer_get_char_count(buffer)==0);
}

static const char *demo_questions = "{\"questions\":[\"Should answer() return 42?\",\"Which compiler should build this project?\"]}";
/* Explicit offline fixture: changes an existing header, compiles and tests real C. */
#ifdef G_OS_WIN32
#define DEMO_TEST "bench-test.exe"
#else
#define DEMO_TEST "./bench-test.exe"
#endif
static const char *demo_plan =
    "{\"summary\":\"Updated the existing answer implementation and added an executable regression check.\","
    "\"steps\":[\"Fix the existing header\",\"Build and run the regression check\"],\"files\":["
    "{\"name\":\"src/value.h\",\"content\":\"static int answer(void) { return 42; }\\n\"},"
    "{\"name\":\"src/main.c\",\"content\":\"#include <stdio.h>\\n#include \\\"value.h\\\"\\nint main(void) { if (answer() != 42) { puts(\\\"FAIL\\\"); return 1; } puts(\\\"PASS: answer == 42\\\"); return 0; }\\n\"}],"
    "\"read_files\":[],\"commands\":[{\"kind\":\"build\",\"command\":\"cc -std=c11 -Wall -Wextra src/main.c -o bench-test.exe\",\"timeout_seconds\":60},"
    "{\"kind\":\"test\",\"command\":\"" DEMO_TEST "\",\"timeout_seconds\":10}]}";
static const char *demo_failing_plan =
    "{\"summary\":\"Run the existing regression check before repair\",\"steps\":[\"Build existing source\",\"Run existing test\"],"
    "\"files\":[],\"read_files\":[],\"commands\":[{\"kind\":\"build\",\"command\":\"cc -std=c11 -Wall -Wextra src/main.c -o bench-test.exe\",\"timeout_seconds\":60},"
    "{\"kind\":\"test\",\"command\":\"" DEMO_TEST "\",\"timeout_seconds\":10}]}";

static void log_line(App *a,const char *who,const char *text) {
    if(!a->transcript) a->transcript=g_string_new(NULL);
    g_string_append_printf(a->transcript,"%s\n%s\n\n",who,text);
    gboolean follow=a->following;
    GtkWidget *card=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    gtk_widget_set_name(card,g_str_has_prefix(who,"YOU")?"user-message":(!strcmp(who,"RESULT") || !strcmp(who,"CHANGES"))?"result-card":"message");
    gtk_widget_set_margin_top(card,8); gtk_widget_set_margin_bottom(card,8);
    GtkWidget *title=label(g_str_has_prefix(who,"YOU")?"You":!strcmp(who,"BENCH")?"Bench":who);
    gtk_widget_set_name(title,"message-role"); gtk_box_pack_start(GTK_BOX(card),title,FALSE,FALSE,0);
    if(strlen(text)>1800 || g_str_has_prefix(who,"COMMAND ")) {
        GtkWidget *exp=gtk_expander_new(!strcmp(who,"COMMAND PASSED")?"Command completed · view output":"Details · view output");
        GtkWidget *v=text_view(FALSE,TRUE); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v)),text,-1);
        GtkWidget *s=scroll(v); gtk_widget_set_size_request(s,-1,190); gtk_container_add(GTK_CONTAINER(exp),s);
        gtk_box_pack_start(GTK_BOX(card),exp,FALSE,FALSE,0);
    } else {
        GtkWidget *body=label(text); gtk_label_set_line_wrap(GTK_LABEL(body),TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(body),PANGO_WRAP_WORD_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(body),80); gtk_label_set_selectable(GTK_LABEL(body),TRUE);
        gtk_box_pack_start(GTK_BOX(card),body,FALSE,FALSE,0);
    }
    if((!strcmp(who,"RESULT") || !strcmp(who,"CHANGES")) && a->plan.count) {
        GtkWidget *view=gtk_button_new_with_label("View changes"); gtk_widget_set_halign(view,GTK_ALIGN_START);
        g_signal_connect(view,"clicked",G_CALLBACK(show_changes),a); gtk_box_pack_start(GTK_BOX(card),view,FALSE,FALSE,0);
    }
    gtk_box_pack_start(GTK_BOX(a->chat),card,FALSE,FALSE,0); gtk_widget_show_all(card);
    if(follow) g_idle_add(follow_chat,a);
}
static void status(App *a,const char *text) {
    char **lines=g_strsplit(text,"\n",2); gtk_label_set_text(GTK_LABEL(a->status),lines[0]);
    gtk_widget_set_tooltip_text(a->status,text); g_strfreev(lines);
}
static void snapshot(App *a,const char *path) {
    GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(a->window),0,0,
        gtk_widget_get_allocated_width(a->window),gtk_widget_get_allocated_height(a->window));
    if(image) { gdk_pixbuf_save(image,path,"png",NULL,NULL); g_object_unref(image); }
}
static gboolean smoke_done(gpointer data) {
    App *a=data; snapshot(a,"smoke.png");
    if(a->readable_smoke) {
        double elapsed=(g_get_monotonic_time()-a->execution_started)/(double)G_USEC_PER_SEC;
        g_print("READABLE PACE: %.2f seconds; pause probe=%s\n",elapsed,a->pause_probed?"passed":"missing");
        if(elapsed<8 || !a->pause_probed) a->exit_code=1;
    }
    gtk_main_quit(); return G_SOURCE_REMOVE;
}
static void finish(App *a,gboolean ok,const char *message) {
    a->stage=REPORT; a->failed=!ok;
    gtk_widget_set_sensitive(a->cancel,FALSE); gtk_widget_set_sensitive(a->send,TRUE);
    gtk_button_set_label(GTK_BUTTON(a->send),"Send ↑");
    gtk_widget_set_sensitive(a->input,TRUE);
    a->paused=FALSE; gtk_button_set_label(GTK_BUTTON(a->pause),"Pause");
    gtk_widget_set_sensitive(a->pause,FALSE); gtk_widget_set_sensitive(a->new_chat,TRUE);
    gtk_widget_set_sensitive(a->settings,TRUE);
    char *s=g_strdup_printf("4 / REPORT  ·  %s\n%s\nEdits saved: %u  |  Commands passed: %u  |  Build: %s  |  Tests: %s\nProject: %s\nBackups / log: %s",
        ok?"BUILD + TEST PASSED":"STOPPED / FAILED",message,a->saved,a->commands_passed,
        a->build_ok?"passed":"not passed",a->test_ok?"passed":"not passed",
        a->directory?a->directory:"none",a->project.run_dir?a->project.run_dir:"none");
    if(a->project.root) {
        GError *e=NULL;
        if(!project_log(&a->project,s,&e)) { log_line(a,"LOG ERROR",e->message); g_clear_error(&e); }
    }
    status(a,s); log_line(a,ok?"RESULT":"ERROR",s); g_free(s);
    gtk_widget_set_sensitive(a->folder,a->directory!=NULL);
    archive_chat(a);
    if(a->smoke) {
        a->exit_code=ok?0:1;
        /* Verify the UI's actual saved artifacts, not merely the fixture parser. */
        for(guint i=0;ok && i<a->plan.count;i++) {
            char *path=g_build_filename(a->directory,a->plan.files[i].name,NULL), *actual=NULL;
            if(!g_file_get_contents(path,&actual,NULL,NULL) || strcmp(actual,a->plan.files[i].content)) a->exit_code=1;
            g_free(actual); g_free(path);
        }
        if(ok && (!a->build_ok || !a->test_ok || !a->cursor_image)) a->exit_code=1;
        if(ok && a->smoke_repair && (a->rounds!=2 || !strstr(a->feedback->str,"Exit: 1"))) a->exit_code=1;
        if(ok && a->project.run_dir) {
            char *backup=g_build_filename(a->project.run_dir,"original","src","value.h",NULL), *original=NULL;
            if(!g_file_get_contents(backup,&original,NULL,NULL) || !strstr(original,"return 1")) a->exit_code=1;
            g_free(backup); g_free(original);
        }
        g_print("SMOKE %s: %u/%u files; %s\n",a->exit_code==0?"PASS":"FAIL",a->saved,a->plan.count,message);
        g_timeout_add(500,smoke_done,a);
    }
}
static void target_widget(App *a,GtkWidget *w) {
    int x=0,y=0;
    gtk_widget_translate_coordinates(w,a->overlay,gtk_widget_get_allocated_width(w)/2,
                                    gtk_widget_get_allocated_height(w)/2,&x,&y);
    a->tx=x; a->ty=y;
}
static void target_caret(App *a) {
    GtkTextView *v=GTK_TEXT_VIEW(a->editor); GtkTextBuffer *b=gtk_text_view_get_buffer(v);
    GtkTextIter end; GdkRectangle rect; int wx,wy,x=0,y=0;
    gtk_text_buffer_get_end_iter(b,&end);
    gtk_text_view_get_iter_location(v,&end,&rect);
    gtk_text_view_buffer_to_window_coords(v,GTK_TEXT_WINDOW_WIDGET,rect.x,rect.y,&wx,&wy);
    gtk_widget_translate_coordinates(a->editor,a->overlay,wx+6,wy+rect.height,&x,&y);
    a->tx=CLAMP(x,10,gtk_widget_get_allocated_width(a->overlay)-24);
    a->ty=CLAMP(y,10,gtk_widget_get_allocated_height(a->overlay)-24);
}
static gboolean draw_cursor(GtkWidget *w,cairo_t *cr,gpointer data) {
    (void)w; App *a=data;
    if(a->stage!=EXECUTION || a->pending || (a->paused && gtk_notebook_get_current_page(GTK_NOTEBOOK(a->notebook))==1)) return FALSE;
    cairo_translate(cr,a->x,a->y);
    if(a->pulse>0) {
        cairo_set_source_rgba(cr,0.39,0.93,0.75,a->pulse);
        cairo_set_line_width(cr,2); cairo_arc(cr,0,0,28*(1-a->pulse)+5,0,2*G_PI); cairo_stroke(cr);
    }
    if(a->cursor_image) {
        gdk_cairo_set_source_pixbuf(cr,a->cursor_image,-a->hot_x,-a->hot_y); cairo_paint(cr); return FALSE;
    }
    cairo_move_to(cr,0,0); cairo_line_to(cr,0,23); cairo_line_to(cr,6,17);
    cairo_line_to(cr,11,28); cairo_line_to(cr,16,25); cairo_line_to(cr,11,15);
    cairo_line_to(cr,20,15); cairo_close_path(cr);
    cairo_set_source_rgb(cr,0.43,0.95,0.77); cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr,0.04,0.1,0.09); cairo_set_line_width(cr,1.5); cairo_stroke(cr);
    return FALSE;
}
static void choose_file(GtkButton *button,gpointer data) {
    App *a=data; guint i=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),"index"));
    if(i>=a->plan.count) return;
    /* During animation only the agent-selected file may be opened. */
    if(a->stage==EXECUTION && (i!=a->index || a->motion!=OPEN_FILE)) return;
    GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->editor));
    gtk_text_buffer_set_text(b,a->stage==EXECUTION?"":a->plan.files[i].content,-1);
}
static void change_selected(GtkComboBox *combo,gpointer data) {
    App *a=data; int index=gtk_combo_box_get_active(combo);
    if(index<0 || (guint)index>=a->plan.count) return;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->before)),a->originals[index]?a->originals[index]:"(New file)",-1);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->after)),a->plan.files[index].content,-1);
}
static void show_changes(GtkButton *b,gpointer data) {
    (void)b; App *a=data; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->workspace_toggle),TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook),1);
}
static void save_current(GtkButton *button,gpointer data) {
    (void)button; App *a=data;
    if(a->stage!=EXECUTION || a->pending || a->motion!=SAVE_FILE) return;
    GError *e=NULL;
    if(!project_write(&a->project,&a->plan.files[a->index],&e)) {
        finish(a,FALSE,e->message); g_clear_error(&e); return;
    }
    a->saved++;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress),(double)(a->index+1)/MAX(a->plan.count,1));
    char *s=g_strdup_printf("Saved %s (%" G_GSIZE_FORMAT " bytes)",a->plan.files[a->index].name,
                           strlen(a->plan.files[a->index].content));
    log_line(a,"WORKSPACE",s); g_free(s);
    if(++a->index==a->plan.count) {
        if(a->plan.commands->len) a->motion=MOVE_RUN;
        else commands_complete(a);
        return;
    }
    a->motion=OPEN_FILE; a->offset=0; a->dwell=0; a->budget=0;
}
static void begin_execution(App *a,const char *json) {
    GError *e=NULL;
    if(!parse_plan(json,&a->plan,&e)) { finish(a,FALSE,e->message); g_clear_error(&e); return; }
    if(a->plan.read_files->len) {
        if(++a->read_rounds>3) { finish(a,FALSE,"Additional-file inspection limit reached (3 rounds)."); return; }
        GString *extra=g_string_new(a->context);
        for(guint i=0;i<a->plan.read_files->len;i++) {
            const char *name=g_ptr_array_index(a->plan.read_files,i); char *text=project_read(&a->project,name,&e);
            if(!text) { g_string_free(extra,TRUE); finish(a,FALSE,e->message); g_clear_error(&e); return; }
            if(extra->len+strlen(text)>1500*1024) {
                g_free(text); g_string_free(extra,TRUE); finish(a,FALSE,"Additional source context exceeds 1.5 MiB."); return;
            }
            g_string_append_printf(extra,"\n--- FILE %s ---\n%s\n--- END FILE ---\n",name,text); g_free(text);
        }
        g_free(a->context); a->context=g_string_free(extra,FALSE);
        request(a,TRUE,NULL); return;
    }
    if(++a->rounds>3) { finish(a,FALSE,"Stopped after 3 execution rounds without passing both build and tests."); return; }
    if(!project_check(&a->project,&a->plan,&e)) { finish(a,FALSE,e->message); g_clear_error(&e); return; }
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(a->change_select));
    for(guint i=0;i<MAX_FILES;i++) g_clear_pointer(&a->originals[i],g_free);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->workspace_toggle),TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook),a->plan.count?0:2);
    GList *children=gtk_container_get_children(GTK_CONTAINER(a->files));
    for(GList *i=children;i;i=i->next) gtk_widget_destroy(i->data);
    g_list_free(children); memset(a->file_buttons,0,sizeof a->file_buttons);
    GString *steps=g_string_new(NULL);
    for(guint i=0;i<a->plan.steps->len;i++) g_string_append_printf(steps,"%u. %s\n",i+1,(char *)g_ptr_array_index(a->plan.steps,i));
    log_line(a,"PLAN",steps->str); g_string_free(steps,TRUE);
    for(guint i=0;i<a->plan.count;i++) {
        char *path=a->project.run_dir?g_build_filename(a->project.run_dir,"original",a->plan.files[i].name,NULL):NULL;
        if(!path || !g_file_get_contents(path,&a->originals[i],NULL,NULL)) {
            g_free(path); path=g_build_filename(a->directory,a->plan.files[i].name,NULL);
            g_file_get_contents(path,&a->originals[i],NULL,NULL);
        }
        g_free(path);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a->change_select),a->plan.files[i].name);
        GtkWidget *b=gtk_button_new_with_label(a->plan.files[i].name);
        g_object_set_data(G_OBJECT(b),"index",GUINT_TO_POINTER(i));
        g_signal_connect(b,"clicked",G_CALLBACK(choose_file),a);
        gtk_box_pack_start(GTK_BOX(a->files),b,FALSE,FALSE,0); a->file_buttons[i]=b;
    }
    gtk_widget_show_all(a->files);
    if(a->plan.count) {
        GString *changes=g_string_new(NULL); g_string_append_printf(changes,"%u files in this change\n",a->plan.count);
        for(guint i=0;i<a->plan.count;i++) g_string_append_printf(changes,"\n%s",a->plan.files[i].name);
        log_line(a,"CHANGES",changes->str); g_string_free(changes,TRUE);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->change_select),a->plan.count?0:-1);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress),0);
    gtk_widget_set_sensitive(a->pause,TRUE);
    a->stage=EXECUTION; a->motion=a->plan.count?OPEN_FILE:MOVE_RUN; a->index=0;
    a->command_index=0; a->build_ok=FALSE; a->test_ok=FALSE;
    a->offset=0; a->dwell=0; a->budget=0;
    a->hold=0;
    if(!a->execution_started) a->execution_started=g_get_monotonic_time();
    status(a,"3 / EXECUTION  ·  Editing the selected project, then running its build and tests.");
}
static void next_round(App *a,const char *reason) {
    log_line(a,"EXECUTION FEEDBACK",reason);
    if(a->rounds>=3 || (a->demo && !a->smoke_repair)) { finish(a,FALSE,reason); return; }
    g_string_append_printf(a->feedback,"\n%s\nReturn a corrected plan and rerun both build and tests.\n",reason);
    GError *e=NULL; char *context=project_context(&a->project,&e);
    if(!context) { finish(a,FALSE,e->message); g_clear_error(&e); return; }
    g_free(a->context); a->context=context; request(a,TRUE,NULL);
}
static void commands_complete(App *a) {
    if(a->build_ok && a->test_ok) finish(a,TRUE,a->plan.summary);
    else next_round(a,"This round did not pass both a real build command and a real test command. No success is claimed.");
}
static void run_command(GtkButton *button,gpointer data) {
    (void)button; App *a=data;
    if(a->stage!=EXECUTION || a->pending || a->runner.active || a->motion!=RUN_COMMAND) return;
    AgentCommand *cmd=g_ptr_array_index(a->plan.commands,a->command_index);
    char *heading=g_strdup_printf("[%s] %s\nTimeout: %u s",cmd->kind,cmd->command,cmd->timeout);
    log_line(a,"RUNNING",heading); status(a,heading); g_free(heading);
    GError *e=NULL;
    char *entry=g_strdup_printf("\n$ [%s] %s\n",cmd->kind,cmd->command);
    if(!project_log(&a->project,entry,&e)) { g_free(entry); finish(a,FALSE,e->message); g_clear_error(&e); return; }
    g_free(entry);
    if(!runner_start(&a->runner,a->directory,cmd->command,cmd->timeout,&e)) {
        char *message=g_strdup(e->message); g_clear_error(&e); next_round(a,message); g_free(message); return;
    }
    a->displayed_bytes=G_MAXUINT64;
    a->motion=WAIT_COMMAND;
}
static void poll_command(App *a) {
    gboolean done=runner_poll(&a->runner);
    if(!done && a->runner.output_bytes==a->displayed_bytes) return;
    a->displayed_bytes=a->runner.output_bytes;
    char *text=g_utf8_make_valid(a->runner.output->str,a->runner.output->len);
    GtkTextBuffer *buf=gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->terminal));
    gtk_text_buffer_set_text(buf,text,-1);
    GtkTextIter end; gtk_text_buffer_get_end_iter(buf,&end); gtk_text_buffer_place_cursor(buf,&end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(a->terminal),gtk_text_buffer_get_insert(buf));
    if(!done) { g_free(text); return; }
    AgentCommand *cmd=g_ptr_array_index(a->plan.commands,a->command_index);
    gboolean ok=a->runner.exit_code==0 && !a->runner.cancelled;
    char *result=g_strdup_printf("%s\nExit code: %d%s%s\n%s\n",cmd->command,a->runner.exit_code,
        a->runner.timed_out?" (timeout)":a->runner.cancelled?" (cancelled)":"",
        a->runner.truncated?" (output truncated to last 256 KiB)":"",text);
    log_line(a,ok?"COMMAND PASSED":"COMMAND FAILED",result);
    GError *e=NULL;
    if(!project_log(&a->project,result,&e)) {
        finish(a,FALSE,e->message); g_clear_error(&e); g_free(result); g_free(text); return;
    }
    /* Feed bounded actual output to the next model round, keeping the useful tail. */
    g_string_append_printf(a->feedback,"\nCommand [%s]: %s\nExit: %d; timeout=%d; cancelled=%d\nOutput:\n%s\n",
        cmd->kind,cmd->command,a->runner.exit_code,a->runner.timed_out,a->runner.cancelled,
        strlen(text)>64000?text+strlen(text)-64000:text);
    if(a->feedback->len>192000) g_string_erase(a->feedback,0,a->feedback->len-192000);
    g_free(text);
    if(a->stopping) finish(a,FALSE,"Cancelled. The command process tree was stopped. Saved edits and backups remain.");
    else if(!ok) next_round(a,result);
    else {
        a->commands_passed++;
        if(!strcmp(cmd->kind,"build")) a->build_ok=TRUE;
        if(!strcmp(cmd->kind,"test")) a->test_ok=TRUE;
        if(++a->command_index>=a->plan.commands->len) commands_complete(a);
        else { a->motion=MOVE_RUN; a->dwell=0; }
    }
    g_free(result);
}
static void show_questions(App *a,const char *json) {
    GError *e=NULL;
    if(!parse_questions(json,&a->questions,&e)) { finish(a,FALSE,e->message); g_clear_error(&e); return; }
    GString *s=g_string_new(NULL);
    for(guint i=0;i<a->questions.count;i++) g_string_append_printf(s,"%u. %s\n",i+1,a->questions.text[i]);
    a->question_text=g_string_free(s,FALSE);
    log_line(a,"CLARIFY",a->question_text);
    a->stage=CLARIFICATION;
    gtk_widget_set_sensitive(a->input,TRUE); gtk_widget_set_sensitive(a->send,TRUE);
    gtk_button_set_label(GTK_BUTTON(a->send),"Answer & run");
    status(a,"2 / CLARIFICATION\nAnswer & run permits project edits and real shell commands with your user permissions.");
    gtk_widget_grab_focus(a->input);
}
static void request(App *a,gboolean planning,const char *answers) {
    if(answers) { g_free(a->answers); a->answers=g_strdup(answers); }
    a->planning=planning;
    gtk_widget_set_sensitive(a->input,FALSE); gtk_widget_set_sensitive(a->send,FALSE);
    gtk_widget_set_sensitive(a->cancel,TRUE);
    gtk_widget_set_sensitive(a->new_chat,FALSE); gtk_widget_set_sensitive(a->settings,FALSE);
    status(a,planning?"3 / EXECUTION  ·  Waiting for the implementation plan…":"2 / CLARIFICATION  ·  Asking the model…");
    if(a->demo) {
        if(planning) begin_execution(a,a->smoke_repair && !a->rounds?demo_failing_plan:demo_plan);
        else show_questions(a,demo_questions);
        return;
    }
    const char *model=gtk_entry_get_text(GTK_ENTRY(a->model));
    if(!*model) { finish(a,FALSE,"Enter a Chat Completions model ID."); return; }
    char *feedback=a->feedback?g_utf8_make_valid(a->feedback->str,-1):NULL;
    char *payload=make_request(model,a->task,a->question_text,planning?a->answers:NULL,a->context,feedback);
    g_free(feedback);
    if(!api_start(&a->api,a->api_key,payload)) { finish(a,FALSE,a->api.error); api_clear(&a->api); return; }
    a->pending=TRUE;
}
static void clear_run(App *a) {
    api_clear(&a->api); a->pending=FALSE; plan_clear(&a->plan); questions_clear(&a->questions);
    g_clear_pointer(&a->task,g_free); g_clear_pointer(&a->question_text,g_free); g_clear_pointer(&a->directory,g_free);
    g_clear_pointer(&a->context,g_free); g_clear_pointer(&a->answers,g_free);
    if(a->feedback) { g_string_free(a->feedback,TRUE); a->feedback=NULL; }
    runner_clear(&a->runner); project_clear(&a->project);
    GList *children=gtk_container_get_children(GTK_CONTAINER(a->files));
    for(GList *i=children;i;i=i->next) gtk_widget_destroy(i->data);
    g_list_free(children); memset(a->file_buttons,0,sizeof a->file_buttons);
    a->saved=0; a->index=0; a->pulse=0;
    a->rounds=a->read_rounds=a->commands_passed=0;
    a->build_ok=a->test_ok=a->stopping=FALSE;
    a->captured_cursor=FALSE;
    a->following=TRUE; a->pause_probed=FALSE; a->execution_started=0;
    a->paused=FALSE; a->hold=0;
    gtk_button_set_label(GTK_BUTTON(a->pause),"Pause"); gtk_widget_set_sensitive(a->pause,FALSE);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(a->change_select));
    for(guint i=0;i<MAX_FILES;i++) g_clear_pointer(&a->originals[i],g_free);
    if(a->transcript) g_string_truncate(a->transcript,0);
}
static gboolean prepare_project(App *a,GError **e) {
    const char *selected=gtk_entry_get_text(GTK_ENTRY(a->project_entry));
    char *root=NULL;
    if(a->demo || !*selected) {
        char *cwd=g_get_current_dir(); root=g_build_filename(cwd,"bench-output-XXXXXX",NULL); g_free(cwd);
        if(!g_mkdtemp(root)) { g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create project directory"); g_free(root); return FALSE; }
    } else root=g_strdup(selected);
    gboolean ok=project_open(&a->project,root,e); g_free(root); if(!ok) return FALSE;
    a->directory=g_strdup(a->project.root); a->feedback=g_string_new(NULL);
    char *base=g_path_get_basename(a->directory); gtk_label_set_text(GTK_LABEL(a->project_name),base); g_free(base);
    if(a->demo) {
        char *src=g_build_filename(a->directory,"src",NULL), *header=g_build_filename(src,"value.h",NULL);
        if(g_mkdir(src,0755)!=0 || !g_file_set_contents(header,"static int answer(void) { return 1; }\n",-1,e)) {
            if(!*e) g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create demo fixture");
            g_free(src); g_free(header); return FALSE;
        }
        g_free(src); g_free(header);
        char *main_path=g_build_filename(a->directory,"src","main.c",NULL);
        const char *original="#include <stdio.h>\n#include \"value.h\"\nint main(void) { if (answer() != 42) { puts(\"FAIL\"); return 1; } puts(\"PASS: answer == 42\"); return 0; }\n";
        gboolean written=g_file_set_contents(main_path,original,-1,e); g_free(main_path);
        if(!written) return FALSE;
    }
    a->context=project_context(&a->project,e); if(!a->context) return FALSE;
    char *message=g_strdup_printf("%s\nRead %u source files; skipped/limited %u entries. Source context is sent to OpenAI in live mode.",
        a->directory,a->project.read_count,a->project.skipped);
    log_line(a,"PROJECT",message); g_free(message);
    gtk_widget_set_sensitive(a->project_entry,FALSE); gtk_widget_set_sensitive(a->browse,FALSE);
    gtk_widget_set_sensitive(a->folder,TRUE);
    return TRUE;
}
static char *input_text(App *a) {
    GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)); GtkTextIter start,end;
    gtk_text_buffer_get_bounds(b,&start,&end); return gtk_text_buffer_get_text(b,&start,&end,FALSE);
}
static void reset_inputs(App *a,gboolean wipe) {
    char *history=(!wipe && a->transcript)?g_strdup(a->transcript->str):NULL;
    if(!wipe && a->directory) gtk_entry_set_text(GTK_ENTRY(a->project_entry),a->directory);
    clear_run(a); a->stage=TASK_INPUT;
    if(history) { g_string_assign(a->transcript,history); g_free(history); }
    if(wipe) empty_box(a->chat);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->editor)),"",-1);
    gtk_widget_set_sensitive(a->input,TRUE); gtk_widget_set_sensitive(a->model,TRUE);
    gtk_widget_set_sensitive(a->project_entry,TRUE); gtk_widget_set_sensitive(a->browse,TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->terminal)),"",-1);
    gtk_widget_set_sensitive(a->folder,FALSE); gtk_button_set_label(GTK_BUTTON(a->send),"Send ↑");
    gtk_label_set_text(GTK_LABEL(a->heading),"New chat");
    gtk_widget_set_sensitive(a->settings,TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->before)),"",-1);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->after)),"",-1);
    status(a,"Ready · choose a project or start a new one");
    project_selected(GTK_EDITABLE(a->project_entry),a);
}
static void submit(GtkButton *button,gpointer data) {
    (void)button; App *a=data;
    if(a->pending || a->stage==EXECUTION) return;
    char *previous=NULL;
    if(a->stage==REPORT) {
        char *draft=input_text(a); g_strstrip(draft);
        if(!*draft) { g_free(draft); return; }
        g_free(draft); previous=g_strndup(a->task?a->task:"",8000);
        reset_inputs(a,FALSE);
    }
    char *text=input_text(a); g_strstrip(text);
    if(!*text || strlen(text)>16000) { status(a,"Enter 1–16000 bytes of task text or answers."); g_free(previous); g_free(text); return; }
    a->following=TRUE; log_line(a,a->stage==TASK_INPUT?"YOU / TASK":"YOU / ANSWERS",text);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)),"",-1);
    if(a->stage==TASK_INPUT) {
        a->task=g_strdup(text); GError *e=NULL;
        char *title=g_strndup(text,80); char *valid=g_utf8_make_valid(title,-1);
        gtk_label_set_text(GTK_LABEL(a->heading),valid); g_free(valid); g_free(title);
        if(!prepare_project(a,&e)) { finish(a,FALSE,e->message); g_clear_error(&e); g_free(previous); g_free(text); return; }
        if(previous) g_string_append_printf(a->feedback,"Follow-up to the previous task: %s\nUse the current project state and the new request.\n",previous);
        gtk_widget_set_sensitive(a->model,FALSE); request(a,FALSE,NULL);
    } else { a->stage=EXECUTION; request(a,TRUE,text); }
    g_free(previous); g_free(text);
}
static void cancel(GtkButton *b,gpointer data) {
    (void)b; App *a=data;
    api_clear(&a->api); a->pending=FALSE;
    if(a->runner.active) {
        a->stopping=TRUE; runner_cancel(&a->runner); gtk_widget_set_sensitive(a->cancel,FALSE);
        status(a,"Stopping command process tree…"); return;
    }
    finish(a,FALSE,"Cancelled. Saved project edits remain; original file backups are in .bench-agent. No command is running.");
}
static void open_folder(GtkButton *b,gpointer data) {
    (void)b; App *a=data;
    const char *selected=a->directory?a->directory:gtk_entry_get_text(GTK_ENTRY(a->project_entry));
    if(!*selected) return;
    char *absolute=g_canonicalize_filename(selected,NULL);
    GError *e=NULL; char *uri=g_filename_to_uri(absolute,NULL,&e); g_free(absolute);
    if(uri) gtk_show_uri_on_window(GTK_WINDOW(a->window),uri,GDK_CURRENT_TIME,&e);
    if(e) { log_line(a,"FOLDER ERROR",e->message); g_error_free(e); }
    g_free(uri);
}
static gboolean smoke_resume(gpointer data) {
    App *a=data; snapshot(a,"smoke-changes.png");
    a->pause_probed=TRUE; toggle_pause(GTK_BUTTON(a->pause),a);
    return G_SOURCE_REMOVE;
}
static gboolean smoke_pause_check(gpointer data) {
    App *a=data;
    g_assert_true(a->paused); g_assert_cmpuint(a->offset,==,a->probe_offset);
    g_assert_true(a->x==a->probe_x && a->y==a->probe_y);
    show_changes(NULL,a); gtk_combo_box_set_active(GTK_COMBO_BOX(a->change_select),0);
    GtkTextIter start,end; GtkTextBuffer *buf=gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->before));
    gtk_text_buffer_get_bounds(buf,&start,&end); char *text=gtk_text_buffer_get_text(buf,&start,&end,FALSE);
    g_assert_nonnull(strstr(text,"return 1")); g_free(text);
    buf=gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->after)); gtk_text_buffer_get_bounds(buf,&start,&end);
    text=gtk_text_buffer_get_text(buf,&start,&end,FALSE); g_assert_nonnull(strstr(text,"return 42")); g_free(text);
    g_timeout_add(150,smoke_resume,a); return G_SOURCE_REMOVE;
}
static gboolean tick(gpointer data) {
    App *a=data; gint64 now=g_get_monotonic_time();
    double dt=MIN((now-a->previous)/1000000.0,0.1); a->previous=now;
    if(a->pending) {
        int result=api_poll(&a->api);
        if(result) {
            a->pending=FALSE;
            char *content=NULL, *failure=NULL;
            if(result<0) failure=g_strdup(a->api.error);
            else if(a->api.status<200 || a->api.status>=300) {
                /* Do not display arbitrary server bodies which may echo secrets. */
                failure=g_strdup_printf("OpenAI HTTP %ld. %s",a->api.status,
                    a->api.status==401?"Check OPENAI_API_KEY.":a->api.status==429?"Rate limit or quota exceeded; check billing and retry later.":
                    a->api.status==404?"Check the model ID and account access.":"Request rejected or service unavailable. Check model compatibility; retry with a new task.");
            } else {
                GError *e=NULL; content=completion_content(a->api.body->str,&e);
                if(!content) { failure=g_strdup(e->message); g_clear_error(&e); }
            }
            /* Release the OLD request before handlers can launch a read/repair round. */
            api_clear(&a->api);
            if(failure) { finish(a,FALSE,failure); g_free(failure); }
            else { if(a->planning) begin_execution(a,content); else show_questions(a,content); }
            g_free(content);
        }
    }
    if(a->runner.active) poll_command(a);
    gboolean fast_test=a->smoke && !a->readable_smoke;
    double rate=gtk_range_get_value(GTK_RANGE(a->speed));
    if(a->stage==EXECUTION && !a->pending && a->plan.commands && a->motion!=WAIT_COMMAND) {
        int wanted=(a->motion==MOVE_RUN || a->motion==RUN_COMMAND)?2:0;
        if(!gtk_widget_get_visible(a->workspace) || gtk_notebook_get_current_page(GTK_NOTEBOOK(a->notebook))!=wanted) {
            a->paused=TRUE; gtk_button_set_label(GTK_BUTTON(a->pause),"Resume");
        }
        if(!a->paused) {
            if(a->motion==OPEN_FILE || a->motion==HOLD_OPEN) {
                GtkAllocation allocation; gtk_widget_get_allocation(a->file_buttons[a->index],&allocation);
                GtkAdjustment *adj=gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(a->tabs));
                gtk_adjustment_clamp_page(adj,allocation.x,allocation.x+allocation.width);
                target_widget(a,a->file_buttons[a->index]);
            } else if(a->motion==MOVE_SAVE || a->motion==SAVE_FILE) target_widget(a,a->save);
            else if(a->motion==MOVE_RUN || a->motion==RUN_COMMAND) target_widget(a,a->run);
            else target_caret(a);
            double dx=a->tx-a->x,dy=a->ty-a->y,distance=hypot(dx,dy);
            /* Velocity cap: a long move really takes time, independent of frame rate.
             * Ease only the last few pixels so the cursor settles without snapping. */
            double step=MIN(distance,MIN(180*rate,distance*5)*dt*(fast_test?25:1));
            if(distance>0.01) { a->x+=dx/distance*step; a->y+=dy/distance*step; }
            gboolean arrived=distance<3;
            double elapsed=dt*(fast_test?60:rate);
            if(a->motion==OPEN_FILE && arrived) {
                gtk_button_clicked(GTK_BUTTON(a->file_buttons[a->index]));
                a->pulse=1; a->motion=HOLD_OPEN; a->hold=0;
                char *message=g_strdup_printf("Opening %s",a->plan.files[a->index].name); status(a,message); g_free(message);
            } else if(a->motion==HOLD_OPEN && (a->hold+=elapsed)>=0.65) {
                a->motion=MOVE_EDITOR; a->hold=0;
            } else if(a->motion==MOVE_EDITOR && arrived) {
                a->motion=TYPE_FILE; a->budget=0;
            } else if(a->motion==TYPE_FILE) {
                const char *content=a->plan.files[a->index].content;
                a->budget+=dt*(fast_test?100000:32*rate);
                gsize start=a->offset;
                while(a->budget>=1 && content[a->offset]) { a->offset=(gsize)(g_utf8_next_char(content+a->offset)-content); a->budget--; }
                if(a->offset>start) {
                    GtkTextBuffer *buf=gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->editor)); GtkTextIter end;
                    gtk_text_buffer_get_end_iter(buf,&end); gtk_text_buffer_insert(buf,&end,content+start,(int)(a->offset-start));
                    gtk_text_buffer_get_end_iter(buf,&end); gtk_text_buffer_place_cursor(buf,&end);
                    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(a->editor),gtk_text_buffer_get_insert(buf));
                }
                if(a->readable_smoke && !a->pause_probed && a->offset>=10 && !a->probe_offset) {
                    toggle_pause(GTK_BUTTON(a->pause),a); a->probe_offset=a->offset; a->probe_x=a->x; a->probe_y=a->y;
                    g_timeout_add(350,smoke_pause_check,a);
                }
                if(!content[a->offset]) { a->motion=REVIEW_FILE; a->hold=0; status(a,"Reviewing the completed edit before saving…"); }
            } else if(a->motion==REVIEW_FILE && (a->hold+=elapsed)>=1.8) a->motion=MOVE_SAVE;
            else if(a->motion==MOVE_SAVE && arrived) { a->motion=SAVE_FILE; a->hold=0; }
            else if(a->motion==SAVE_FILE && (a->hold+=elapsed)>=1.0) {
                a->pulse=1; gtk_button_clicked(GTK_BUTTON(a->save));
                if(a->motion==MOVE_RUN) gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook),2);
            } else if(a->motion==MOVE_RUN && arrived) { a->motion=RUN_COMMAND; a->hold=0; }
            else if(a->motion==RUN_COMMAND) {
                a->hold+=elapsed;
                AgentCommand *cmd=g_ptr_array_index(a->plan.commands,a->command_index);
                char *preview=g_strdup_printf("Next · %s",cmd->command); status(a,preview); g_free(preview);
                if(a->smoke && !a->captured_cursor) { snapshot(a,"smoke-cursor.png"); a->captured_cursor=TRUE; }
                if(a->hold>=1.2) { a->pulse=1; gtk_button_clicked(GTK_BUTTON(a->run)); }
            }
        }
    }
    gtk_widget_set_sensitive(a->save,a->stage==EXECUTION && !a->pending && !a->paused && a->motion==SAVE_FILE);
    gtk_widget_set_sensitive(a->run,a->stage==EXECUTION && !a->pending && !a->paused && a->motion==RUN_COMMAND);
    a->pulse=MAX(0,a->pulse-dt*2.5); gtk_widget_queue_draw(a->cursor);
    return G_SOURCE_CONTINUE;
}
static GtkWidget *text_view(gboolean editable,gboolean wrap) {
    GtkWidget *v=gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(v),editable);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(v),editable);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v),wrap?GTK_WRAP_WORD_CHAR:GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(v),16); gtk_text_view_set_right_margin(GTK_TEXT_VIEW(v),16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(v),14); gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(v),14);
    return v;
}
static GtkWidget *scroll(GtkWidget *child) {
    GtkWidget *s=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(s),child);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC); return s;
}
static GtkWidget *label(const char *s) {
    GtkWidget *l=gtk_label_new(s); gtk_label_set_xalign(GTK_LABEL(l),0); return l;
}
static void destroy(GtkWidget *w,gpointer data) {
    (void)w; App *a=data; runner_clear(&a->runner); gtk_main_quit();
}
static void browse_project(GtkButton *b,gpointer data) {
    (void)b; App *a=data;
    GtkWidget *dialog=gtk_file_chooser_dialog_new("Choose existing project",GTK_WINDOW(a->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Select project",GTK_RESPONSE_ACCEPT,NULL);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
        char *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(a->project_entry),path); g_free(path);
    }
    gtk_widget_destroy(dialog);
}
static void project_selected(GtkEditable *entry,gpointer data) {
    App *a=data; const char *path=gtk_entry_get_text(GTK_ENTRY(entry));
    char *base=*path?g_path_get_basename(path):g_strdup("New project");
    gtk_label_set_text(GTK_LABEL(a->project_name),base); g_free(base);
    gtk_widget_set_sensitive(a->folder,*path && g_file_test(path,G_FILE_TEST_IS_DIR));
}
static void history_open(GtkButton *b,gpointer data) {
    App *a=data; const char *text=g_object_get_data(G_OBJECT(b),"transcript");
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Saved conversation · read only",GTK_WINDOW(a->window),
        GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog),760,620);
    GtkWidget *v=text_view(FALSE,TRUE); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v)),text,-1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll(v),TRUE,TRUE,0);
    gtk_widget_show_all(dialog); gtk_dialog_run(GTK_DIALOG(dialog)); gtk_widget_destroy(dialog);
}
static void archive_chat(App *a) {
    if(!a->task || !a->transcript) return;
    GtkWidget *b=gtk_button_new(); GtkWidget *l=label(a->task);
    gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(l),23);
    gtk_container_add(GTK_CONTAINER(b),l); gtk_widget_set_name(b,"history-item");
    g_object_set_data_full(G_OBJECT(b),"transcript",g_strdup(a->transcript->str),g_free);
    g_signal_connect(b,"clicked",G_CALLBACK(history_open),a);
    gtk_box_pack_start(GTK_BOX(a->history),b,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(a->history),b,0);
    gtk_widget_set_tooltip_text(b,"Read this completed conversation. History lasts for this app session.");
    gtk_widget_show_all(b);
}
static void new_chat(GtkButton *b,gpointer data) {
    (void)b; App *a=data;
    if(a->pending || a->stage==EXECUTION) return;
    reset_inputs(a,TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)),"",-1);
    log_line(a,"BENCH","What would you like to build?\nChoose a project in the sidebar, then describe your task below.");
    gtk_widget_grab_focus(a->input);
}
static void toggle_workspace(GtkToggleButton *b,gpointer data) {
    App *a=data; gboolean show=gtk_toggle_button_get_active(b);
    gtk_widget_set_visible(a->workspace,show);
    if(!show && a->stage==EXECUTION) { a->paused=TRUE; gtk_button_set_label(GTK_BUTTON(a->pause),"Resume"); }
}
static void toggle_pause(GtkButton *b,gpointer data) {
    App *a=data; a->paused=!a->paused;
    gtk_button_set_label(b,a->paused?"Resume":"Pause");
    if(!a->paused) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->workspace_toggle),TRUE);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook),(a->motion>=MOVE_RUN)?2:0);
    }
}
static void pace_changed(GtkRange *range,gpointer data) {
    App *a=data; double rate=gtk_range_get_value(range);
    char *text=g_strdup_printf("%.2g×  ·  %.0f characters/s",rate,32*rate);
    gtk_label_set_text(GTK_LABEL(a->pace_label),text); g_free(text);
}
static gboolean composer_key(GtkWidget *w,GdkEventKey *e,gpointer data) {
    (void)w; App *a=data;
    if((e->keyval==GDK_KEY_Return || e->keyval==GDK_KEY_KP_Enter) && !(e->state&GDK_SHIFT_MASK)) {
        if(gtk_widget_get_sensitive(a->send)) submit(NULL,a);
        return TRUE;
    }
    return FALSE;
}
static void settings_open(GtkButton *b,gpointer data) {
    (void)b; App *a=data;
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Connection settings",GTK_WINDOW(a->window),GTK_DIALOG_MODAL,
        "Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_ACCEPT,NULL);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)); gtk_container_set_border_width(GTK_CONTAINER(box),24);
    gtk_box_set_spacing(GTK_BOX(box),12); gtk_window_set_default_size(GTK_WINDOW(dialog),480,280);
    gtk_box_pack_start(GTK_BOX(box),label("OpenAI API key · kept in memory for this session"),FALSE,FALSE,0);
    GtkWidget *key=gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(key),FALSE);
    gtk_entry_set_text(GTK_ENTRY(key),a->api_key?a->api_key:""); gtk_box_pack_start(GTK_BOX(box),key,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),label("Chat Completions model"),FALSE,FALSE,0);
    GtkWidget *model=gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(model),gtk_entry_get_text(GTK_ENTRY(a->model)));
    gtk_box_pack_start(GTK_BOX(box),model,FALSE,FALSE,0);
    gtk_widget_show_all(dialog);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
        g_free(a->api_key); a->api_key=g_strdup(gtk_entry_get_text(GTK_ENTRY(key)));
        gtk_entry_set_text(GTK_ENTRY(a->model),gtk_entry_get_text(GTK_ENTRY(model)));
    }
    gtk_widget_destroy(dialog);
}
static gboolean smoke_start(gpointer data) {
    App *a=data;
    /* Exercise cancellation and reset using the same button handlers as users. */
    a->smoke=FALSE;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)),"Cancellation test",-1);
    submit(NULL,a); g_assert_cmpint(a->stage,==,CLARIFICATION);
    cancel(NULL,a); g_assert_cmpint(a->stage,==,REPORT);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)),"Follow up on the previous task",-1);
    submit(NULL,a); g_assert_cmpint(a->stage,==,CLARIFICATION);
    g_assert_nonnull(strstr(a->feedback->str,"Cancellation test"));
    g_assert_nonnull(strstr(a->transcript->str,"Cancellation test"));
    cancel(NULL,a);
    new_chat(NULL,a); g_assert_cmpint(a->stage,==,TASK_INPUT);
    g_assert_null(a->task); g_assert_null(a->question_text);
    a->smoke=TRUE;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)),"Fix the answer implementation and verify it",-1);
    GdkEventKey key={0}; key.keyval=GDK_KEY_Return;
    g_assert_true(composer_key(a->input,&key,a));
    if(a->stage==CLARIFICATION) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->input)),"Yes, return 42. Build it with GCC and run the regression test.",-1);
        key.state=GDK_SHIFT_MASK; g_assert_false(composer_key(a->input,&key,a));
        g_assert_cmpint(a->stage,==,CLARIFICATION); key.state=0;
        g_assert_true(composer_key(a->input,&key,a));
    }
    return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
    App a={0}; a.following=TRUE; a.api_key=g_strdup(g_getenv("OPENAI_API_KEY"));
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--demo")) a.demo=TRUE;
        else if(!strcmp(argv[i],"--smoke")) { a.demo=TRUE; a.smoke=TRUE; }
        else if(!strcmp(argv[i],"--smoke-readable")) { a.demo=TRUE; a.smoke=TRUE; a.readable_smoke=TRUE; }
        else if(!strcmp(argv[i],"--smoke-repair")) { a.demo=TRUE; a.smoke=TRUE; a.smoke_repair=TRUE; }
        else { g_print("Usage: %s [--demo | --smoke | --smoke-repair | --smoke-readable]\n",argv[0]); return !strcmp(argv[i],"--help")?0:2; }
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) { g_printerr("libcurl initialization failed\n"); return 1; }
    if(!gtk_init_check(NULL,NULL)) { g_printerr("Cannot open desktop display\n"); curl_global_cleanup(); return 1; }
    g_object_set(gtk_settings_get_default(),"gtk-application-prefer-dark-theme",TRUE,NULL);
    GError *cursor_error=NULL;
    const char *custom=g_getenv("BENCH_CURSOR_FILE");
    a.cursor_image=cursor_load(custom?custom:"assets/ai.cur",48,&a.hot_x,&a.hot_y,&cursor_error);
    if(!a.cursor_image && !custom) {
        g_clear_error(&cursor_error); char *exe=g_find_program_in_path(argv[0]);
        if(exe) {
            char *dir=g_path_get_dirname(exe), *asset=g_build_filename(dir,"..","assets","ai.cur",NULL);
            a.cursor_image=cursor_load(asset,48,&a.hot_x,&a.hot_y,&cursor_error);
            g_free(asset); g_free(dir); g_free(exe);
        }
    }
    GtkCssProvider *css=gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "* {font-family:Segoe UI,Arial,sans-serif;font-size:16px;}"
        "window {background:#292a29;color:#ecebea;} label {color:#e7e6e3;}"
        "#sidebar {background:#232525;border-right:1px solid #343635;}"
        "#brand {font-size:24px;font-weight:600;} #heading {font-size:17px;font-weight:600;}"
        "#muted,#message-role {color:#a4a6a2;font-size:12px;} #message-role {font-weight:600;}"
        "#header {border-bottom:1px solid #363836;padding:14px 22px;}"
        "button {background:transparent;border:1px solid transparent;border-radius:9px;color:#e7e6e3;padding:9px 12px;box-shadow:none;}"
        "button:hover {background:#3c3e3b;} button:checked {background:#434540;border-color:#51544e;}"
        "button:disabled label {color:#797d76;} #new-chat {background:#313431;border-color:#41453f;}"
        "#history-item {padding:9px 8px;} #primary {background:#e7e7df;border-radius:18px;}"
        "#primary label {color:#232521;font-weight:600;} #primary:disabled {background:#454841;}"
        "entry {background:#30322f;color:#e6e7e1;border:1px solid #41443e;border-radius:8px;padding:8px;box-shadow:none;}"
        "#composer {background:#393b37;border:1px solid #4a4d46;border-radius:22px;padding:12px;}"
        "textview,textview text {background:#292a29;color:#e7e6e3;} textview {font-size:14px;}"
        "#composer textview,#composer textview text {background:#393b37;font-size:15px;}"
        "#message {padding:12px 8px;} #user-message {background:#363934;border-radius:15px;padding:16px;}"
        "#result-card {background:#30352f;border:1px solid #495344;border-radius:12px;padding:16px;}"
        "#workspace {background:#222423;border-left:1px solid #3d403a;}"
        "#workspace button {border-color:#3e423b;}"
        "#workspace textview,#workspace textview text {background:#222423;font-family:Consolas,Monospace;font-size:13px;}"
        "#workspace notebook header {background:#272a26;border-color:#3b3e38;}"
        "notebook {border:1px solid #41453d;} notebook tab {padding:10px;border-bottom:2px solid transparent;} notebook tab:checked {border-bottom-color:#c8b0a0;background-image:none;box-shadow:inset 0 -2px #c8b0a0;}"
        "#before text {color:#dca6a0;} #after text {color:#b8d4b3;}"
        "#statusbar {border-top:1px solid #363936;padding:9px 18px;} #statusbar label {font-size:12px;color:#b2b8aa;}"
        "progressbar trough {min-height:3px;background:#393d35;} progressbar progress {background:#b7c5a7;background-image:none;border-color:#b7c5a7;min-height:3px;}"
        "scale trough {background:#454a40;min-height:3px;} scale highlight {background:#c4b19e;} scale slider {background:#d6d8cd;min-width:12px;min-height:12px;}"
        "paned separator {background:#3b3e38;min-width:2px;} scrollbar {background:transparent;} scrollbar slider {background:#4c5048;border-radius:8px;min-width:5px;min-height:25px;}"
        "expander {padding:8px;border:1px solid #41463d;border-radius:8px;}"
        , -1,NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    a.window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(a.window),"Bench — coding workspace");
    gtk_window_set_default_size(GTK_WINDOW(a.window),1440,920); gtk_widget_set_size_request(a.window,1060,700);
    g_signal_connect(a.window,"destroy",G_CALLBACK(destroy),&a);
    GtkWidget *root=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); gtk_container_add(GTK_CONTAINER(a.window),root);
    GtkWidget *sidebar=gtk_box_new(GTK_ORIENTATION_VERTICAL,12); gtk_widget_set_name(sidebar,"sidebar");
    gtk_widget_set_size_request(sidebar,218,-1); gtk_container_set_border_width(GTK_CONTAINER(sidebar),18);
    gtk_box_pack_start(GTK_BOX(root),sidebar,FALSE,FALSE,0);
    GtkWidget *brand=label("Bench  ⌄"); gtk_widget_set_name(brand,"brand"); gtk_box_pack_start(GTK_BOX(sidebar),brand,FALSE,FALSE,8);
    a.new_chat=gtk_button_new_with_label("＋  New chat"); gtk_widget_set_name(a.new_chat,"new-chat");
    g_signal_connect(a.new_chat,"clicked",G_CALLBACK(new_chat),&a); gtk_box_pack_start(GTK_BOX(sidebar),a.new_chat,FALSE,FALSE,6);
    GtkWidget *project_caption=label("PROJECT"); gtk_widget_set_name(project_caption,"muted");
    gtk_widget_set_margin_top(project_caption,18); gtk_box_pack_start(GTK_BOX(sidebar),project_caption,FALSE,FALSE,0);
    a.project_name=label("No project selected"); gtk_label_set_ellipsize(GTK_LABEL(a.project_name),PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(a.project_name),22); gtk_box_pack_start(GTK_BOX(sidebar),a.project_name,FALSE,FALSE,0);
    a.browse=gtk_button_new_with_label("▱  Open project"); g_signal_connect(a.browse,"clicked",G_CALLBACK(browse_project),&a);
    gtk_box_pack_start(GTK_BOX(sidebar),a.browse,FALSE,FALSE,0);
    a.project_entry=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(a.project_entry),"Folder path…");
    gtk_entry_set_width_chars(GTK_ENTRY(a.project_entry),16);
    gtk_widget_set_tooltip_text(a.project_entry,"Existing project folder. Leave blank to create a new project.");
    if(g_getenv("BENCH_PROJECT")) gtk_entry_set_text(GTK_ENTRY(a.project_entry),g_getenv("BENCH_PROJECT"));
    gtk_box_pack_start(GTK_BOX(sidebar),a.project_entry,FALSE,FALSE,0);
    GtkWidget *recent=label("RECENT CHATS"); gtk_widget_set_name(recent,"muted"); gtk_widget_set_margin_top(recent,22);
    gtk_box_pack_start(GTK_BOX(sidebar),recent,FALSE,FALSE,0);
    a.history=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); gtk_box_pack_start(GTK_BOX(sidebar),scroll(a.history),TRUE,TRUE,0);
    a.settings=gtk_button_new_with_label("⚙  Connection settings"); g_signal_connect(a.settings,"clicked",G_CALLBACK(settings_open),&a);
    gtk_box_pack_start(GTK_BOX(sidebar),a.settings,FALSE,FALSE,0);
    GtkWidget *mode=label(a.demo?"●  Offline demonstration":"●  Local workspace"); gtk_widget_set_name(mode,"muted");
    gtk_box_pack_start(GTK_BOX(sidebar),mode,FALSE,FALSE,4);
    GtkWidget *main_column=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_box_pack_start(GTK_BOX(root),main_column,TRUE,TRUE,0);
    GtkWidget *header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10); gtk_widget_set_name(header,"header");
    a.heading=label("New chat"); gtk_widget_set_name(a.heading,"heading");
    gtk_label_set_ellipsize(GTK_LABEL(a.heading),PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(a.heading),55);
    gtk_box_pack_start(GTK_BOX(header),a.heading,TRUE,TRUE,0);
    a.folder=gtk_button_new_with_label("Open folder"); gtk_widget_set_sensitive(a.folder,FALSE);
    g_signal_connect(a.folder,"clicked",G_CALLBACK(open_folder),&a); gtk_box_pack_start(GTK_BOX(header),a.folder,FALSE,FALSE,0);
    a.workspace_toggle=gtk_toggle_button_new_with_label("Workspace");
    g_signal_connect(a.workspace_toggle,"toggled",G_CALLBACK(toggle_workspace),&a);
    gtk_box_pack_start(GTK_BOX(header),a.workspace_toggle,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(main_column),header,FALSE,FALSE,0);
    GtkWidget *split=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_paned_set_position(GTK_PANED(split),590);
    gtk_box_pack_start(GTK_BOX(main_column),split,TRUE,TRUE,0);
    GtkWidget *conversation=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_widget_set_size_request(conversation,390,-1);
    gtk_paned_pack1(GTK_PANED(split),conversation,TRUE,FALSE);
    a.chat=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_container_set_border_width(GTK_CONTAINER(a.chat),24);
    a.chat_scroll=scroll(a.chat); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(a.chat_scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    g_signal_connect(a.chat_scroll,"scroll-event",G_CALLBACK(chat_scrolled),&a);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(a.chat_scroll)),"value-changed",G_CALLBACK(chat_position),&a);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(a.chat_scroll)),"changed",G_CALLBACK(chat_extent),&a);
    g_signal_connect(gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(a.chat_scroll)),"button-press-event",G_CALLBACK(scroll_dragged),&a);
    gtk_box_pack_start(GTK_BOX(conversation),a.chat_scroll,TRUE,TRUE,0);
    GtkWidget *composer=gtk_box_new(GTK_ORIENTATION_VERTICAL,6); gtk_widget_set_name(composer,"composer");
    gtk_widget_set_margin_start(composer,22); gtk_widget_set_margin_end(composer,22); gtk_widget_set_margin_bottom(composer,18);
    a.input=text_view(TRUE,TRUE); GtkWidget *input_scroll=scroll(a.input); gtk_widget_set_size_request(input_scroll,-1,100);
    GtkWidget *input_overlay=gtk_overlay_new(); gtk_container_add(GTK_CONTAINER(input_overlay),input_scroll);
    a.placeholder=label("Ask Bench to change something…"); gtk_widget_set_name(a.placeholder,"muted");
    gtk_widget_set_halign(a.placeholder,GTK_ALIGN_START); gtk_widget_set_valign(a.placeholder,GTK_ALIGN_START);
    gtk_widget_set_margin_start(a.placeholder,16); gtk_widget_set_margin_top(a.placeholder,14);
    gtk_overlay_add_overlay(GTK_OVERLAY(input_overlay),a.placeholder);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(input_overlay),a.placeholder,TRUE);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a.input)),"changed",G_CALLBACK(composer_changed),&a);
    gtk_box_pack_start(GTK_BOX(composer),input_overlay,TRUE,TRUE,0);
    g_signal_connect(a.input,"key-press-event",G_CALLBACK(composer_key),&a);
    GtkWidget *composer_actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
    a.model=gtk_entry_new(); gtk_entry_set_width_chars(GTK_ENTRY(a.model),14);
    gtk_entry_set_text(GTK_ENTRY(a.model),g_getenv("OPENAI_MODEL")?g_getenv("OPENAI_MODEL"):"gpt-4o-mini");
    gtk_widget_set_tooltip_text(a.model,"Chat Completions model ID"); gtk_box_pack_start(GTK_BOX(composer_actions),a.model,TRUE,TRUE,0);
    a.cancel=gtk_button_new_with_label("Stop"); gtk_widget_set_sensitive(a.cancel,FALSE);
    g_signal_connect(a.cancel,"clicked",G_CALLBACK(cancel),&a); gtk_box_pack_start(GTK_BOX(composer_actions),a.cancel,FALSE,FALSE,0);
    a.send=gtk_button_new_with_label("Send ↑"); gtk_widget_set_name(a.send,"primary");
    g_signal_connect(a.send,"clicked",G_CALLBACK(submit),&a); gtk_box_pack_start(GTK_BOX(composer_actions),a.send,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(composer),composer_actions,FALSE,FALSE,0);
    GtkWidget *hint=label("Enter to send  ·  Shift+Enter for a new line"); gtk_widget_set_name(hint,"muted");
    gtk_box_pack_start(GTK_BOX(composer),hint,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(conversation),composer,FALSE,FALSE,0);
    a.workspace=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); gtk_widget_set_name(a.workspace,"workspace");
    gtk_widget_set_size_request(a.workspace,440,-1); gtk_paned_pack2(GTK_PANED(split),a.workspace,TRUE,FALSE);
    a.overlay=gtk_overlay_new(); gtk_box_pack_start(GTK_BOX(a.workspace),a.overlay,TRUE,TRUE,0);
    GtkWidget *right=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_container_set_border_width(GTK_CONTAINER(right),14);
    gtk_container_add(GTK_CONTAINER(a.overlay),right);
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_pack_start(GTK_BOX(bar),label("Workspace"),TRUE,TRUE,0);
    a.save=gtk_button_new_with_label("Save edit"); g_signal_connect(a.save,"clicked",G_CALLBACK(save_current),&a);
    gtk_widget_set_tooltip_text(a.save,"The agent saves after the review pause."); gtk_box_pack_start(GTK_BOX(bar),a.save,FALSE,FALSE,0);
    a.run=gtk_button_new_with_label("Run command"); g_signal_connect(a.run,"clicked",G_CALLBACK(run_command),&a);
    gtk_widget_set_tooltip_text(a.run,"The agent starts the queued command after its preview pause.");
    gtk_box_pack_start(GTK_BOX(bar),a.run,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(right),bar,FALSE,FALSE,0);
    a.notebook=gtk_notebook_new(); gtk_box_pack_start(GTK_BOX(right),a.notebook,TRUE,TRUE,0);
    GtkWidget *editor_page=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    a.files=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4); a.tabs=scroll(a.files); gtk_widget_set_size_request(a.tabs,-1,52);
    gtk_box_pack_start(GTK_BOX(editor_page),a.tabs,FALSE,FALSE,0);
    a.editor=text_view(FALSE,FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(a.editor),TRUE);
    gtk_box_pack_start(GTK_BOX(editor_page),scroll(a.editor),TRUE,TRUE,0);
    gtk_notebook_append_page(GTK_NOTEBOOK(a.notebook),editor_page,gtk_label_new("Editor"));
    GtkWidget *changes=gtk_box_new(GTK_ORIENTATION_VERTICAL,10); gtk_container_set_border_width(GTK_CONTAINER(changes),10);
    a.change_select=gtk_combo_box_text_new(); g_signal_connect(a.change_select,"changed",G_CALLBACK(change_selected),&a);
    gtk_box_pack_start(GTK_BOX(changes),a.change_select,FALSE,FALSE,0);
    GtkWidget *comparison=gtk_paned_new(GTK_ORIENTATION_VERTICAL); gtk_paned_set_position(GTK_PANED(comparison),230);
    GtkWidget *old=gtk_box_new(GTK_ORIENTATION_VERTICAL,6), *newer=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
    gtk_box_pack_start(GTK_BOX(old),label("Before · original file"),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(newer),label("After · proposed replacement"),FALSE,FALSE,0);
    a.before=text_view(FALSE,FALSE); gtk_widget_set_name(a.before,"before");
    a.after=text_view(FALSE,FALSE); gtk_widget_set_name(a.after,"after");
    gtk_box_pack_start(GTK_BOX(old),scroll(a.before),TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(newer),scroll(a.after),TRUE,TRUE,0);
    gtk_paned_pack1(GTK_PANED(comparison),old,TRUE,FALSE); gtk_paned_pack2(GTK_PANED(comparison),newer,TRUE,FALSE);
    gtk_box_pack_start(GTK_BOX(changes),comparison,TRUE,TRUE,0); gtk_notebook_append_page(GTK_NOTEBOOK(a.notebook),changes,gtk_label_new("Changes"));
    a.terminal=text_view(FALSE,FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(a.terminal),TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(a.notebook),scroll(a.terminal),gtk_label_new("Terminal"));
    a.progress=gtk_progress_bar_new(); gtk_box_pack_start(GTK_BOX(right),a.progress,FALSE,FALSE,0);
    GtkWidget *pace=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    a.pause=gtk_button_new_with_label("Pause"); gtk_widget_set_sensitive(a.pause,FALSE);
    gtk_widget_set_tooltip_text(a.pause,"Pause cursor, typing and upcoming commands. A command or API request already running continues.");
    g_signal_connect(a.pause,"clicked",G_CALLBACK(toggle_pause),&a); gtk_box_pack_start(GTK_BOX(pace),a.pause,FALSE,FALSE,0);
    a.pace_label=label("1×  ·  32 characters/s"); gtk_widget_set_name(a.pace_label,"muted");
    gtk_box_pack_start(GTK_BOX(pace),a.pace_label,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(right),pace,FALSE,FALSE,0);
    a.speed=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0.25,4.0,0.25); gtk_scale_set_draw_value(GTK_SCALE(a.speed),FALSE);
    gtk_range_set_value(GTK_RANGE(a.speed),1); g_signal_connect(a.speed,"value-changed",G_CALLBACK(pace_changed),&a);
    gtk_widget_set_tooltip_text(a.speed,"Reading pace: 0.25× to 4×. Controls cursor movement, typing and review pauses.");
    gtk_box_pack_start(GTK_BOX(right),a.speed,FALSE,FALSE,0);
    a.cursor=gtk_drawing_area_new(); gtk_widget_set_hexpand(a.cursor,TRUE); gtk_widget_set_vexpand(a.cursor,TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(a.overlay),a.cursor); gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(a.overlay),a.cursor,TRUE);
    g_signal_connect(a.cursor,"draw",G_CALLBACK(draw_cursor),&a);
    GtkWidget *statusbar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0); gtk_widget_set_name(statusbar,"statusbar");
    a.status=label("Ready · choose a project or start a new one"); gtk_label_set_ellipsize(GTK_LABEL(a.status),PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(statusbar),a.status,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(main_column),statusbar,FALSE,FALSE,0);
    g_signal_connect(a.project_entry,"changed",G_CALLBACK(project_selected),&a);
    project_selected(GTK_EDITABLE(a.project_entry),&a);
    log_line(&a,"BENCH",a.demo?"Offline model fixture, real execution. Creates an existing C fixture, edits it, then runs cc and its test executable. Requires a C compiler on PATH.":
        "Select a project and describe your change. Source context and command output are sent to OpenAI. Answer & run authorizes edits and real commands with your user permissions. Original edited files are backed up; build/test failures trigger up to two repair rounds.");
    if(cursor_error) { log_line(&a,"CURSOR ERROR",cursor_error->message); g_clear_error(&cursor_error); }
    a.x=70; a.y=80; a.previous=g_get_monotonic_time(); a.timer=g_timeout_add(16,tick,&a);
    gtk_widget_show_all(a.window);
    gtk_widget_hide(a.workspace);
    if(a.smoke) g_idle_add(smoke_start,&a);
    gtk_main();
    /* No GTK access after window destruction. curl cancellation is synchronous. */
    g_source_remove(a.timer); api_clear(&a.api); plan_clear(&a.plan); questions_clear(&a.questions);
    runner_clear(&a.runner); project_clear(&a.project); g_clear_object(&a.cursor_image);
    g_free(a.context); g_free(a.answers); if(a.feedback) g_string_free(a.feedback,TRUE);
    g_free(a.api_key); if(a.transcript) g_string_free(a.transcript,TRUE);
    for(guint i=0;i<MAX_FILES;i++) g_free(a.originals[i]);
    g_free(a.task); g_free(a.question_text); g_free(a.directory); curl_global_cleanup(); return a.exit_code;
}
