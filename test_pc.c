#include "pc_control.h"
#include "pc_observe.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <stdio.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
static void parser(void){
    GError *e=NULL;PcAction a={0};
    g_assert_true(pc_parse("{\"action\":\"click\",\"reason\":\"Focus field\",\"x\":12,\"y\":34}",&a,&e));g_assert_cmpint(a.x,==,12);pc_action_clear(&a);
    const char *bad[]={"{}","{\"action\":\"shell\",\"reason\":\"x\"}","{\"action\":\"click\",\"reason\":\"x\",\"x\":-1,\"y\":1}","{\"action\":\"click\",\"reason\":\"x\",\"x\":1.5,\"y\":1}","{\"action\":\"key\",\"reason\":\"x\",\"text\":\"Win+R\"}","{\"action\":\"scroll\",\"reason\":\"x\",\"x\":1,\"y\":1,\"delta\":0}",NULL};
    for(int i=0;bad[i];i++){g_assert_false(pc_parse(bad[i],&a,&e));g_assert_nonnull(e);g_clear_error(&e);pc_action_clear(&a);}
}
static void request(void){PcSession s={.image="data:image/png;base64,fixture",.image_width=800,.image_height=600};char *r=pc_request("vision-fixture","Type hello","confirmed",&s,"Clicked field");g_assert_nonnull(strstr(r,"image_url"));g_assert_nonnull(strstr(r,"data:image/png;base64,fixture"));g_assert_nonnull(strstr(r,"800 x 600"));g_free(r);}
static void no_capture(void){PcSession s={0};PcAction a={.kind="click",.x=0,.y=0};GError *e=NULL;g_assert_false(pc_execute(&s,&a,&e));g_assert_nonnull(e);g_clear_error(&e);g_assert_false(pc_revalidate(&s,&e));g_assert_nonnull(e);g_clear_error(&e);}
static void structured_compare(void){
    const char *a="{\"window\":{\"pid\":4},\"focused\":{\"name\":\"Editor\",\"value\":\"a\"},\"cursor\":[1,2],\"controls\":[]}";
    const char *b="{\"window\":{\"pid\":4},\"focused\":{\"name\":\"Editor\",\"value\":\"a\"},\"cursor\":[9,8],\"controls\":[]}";
    const char *changed="{\"window\":{\"pid\":4},\"focused\":{\"name\":\"Editor\",\"value\":\"b\"},\"cursor\":[9,8],\"controls\":[]}";
    g_assert_true(pc_observation_equal(a,b));g_assert_false(pc_observation_equal(a,changed));g_assert_false(pc_observation_equal(a,"{"));g_assert_false(pc_observation_equal(a,NULL));
}
static void typing_guard(void){
    PcSession s={.pid=4,.captured_us=g_get_monotonic_time(),.structured="{\"window\":{\"pid\":4},\"focused\":{\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]}}"};
    g_assert_true(pc_can_type_at(&s,21,31));g_assert_false(pc_can_type_at(&s,120,31));g_assert_false(pc_can_type_at(&s,21,80));s.pid=5;g_assert_false(pc_can_type_at(&s,21,31));s.pid=4;s.structured_thin=TRUE;g_assert_false(pc_can_type_at(&s,21,31));s.structured_thin=FALSE;
    s.structured="{\"window\":{\"pid\":4},\"focused\":{\"role\":\"edit\",\"password\":true,\"bounds\":[20,30,100,50]}}";g_assert_false(pc_can_type_at(&s,21,31));
    s.structured="{\"window\":{\"pid\":4},\"focused\":{\"role\":\"button\",\"password\":false,\"bounds\":[20,30,100,50]}}";g_assert_false(pc_can_type_at(&s,21,31));
}
static void typing_identity(void){
    PcSession s={.pid=4,.captured_us=g_get_monotonic_time(),.structured="{\"window\":{\"pid\":4},\"focused\":null,\"controls\":[{\"id\":\"2.a.11\",\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]}]}"};
    char *id=pc_edit_target_id(&s,21,31);g_assert_cmpstr(id,==,"2.a.11");g_assert_null(pc_edit_target_id(&s,120,31));g_assert_false(pc_can_type_target(&s,21,31,id));
    s.structured="{\"window\":{\"pid\":4},\"focused\":{\"id\":\"2.a.11\",\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]}}";g_assert_true(pc_can_type_target(&s,21,31,id));g_assert_false(pc_can_type_target(&s,21,31,NULL));g_assert_false(pc_can_type_target(&s,21,31,"50004:20:30"));
    s.structured="{\"window\":{\"pid\":4},\"focused\":{\"id\":\"2.a.12\",\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]}}";g_assert_true(pc_can_type_at(&s,21,31));g_assert_false(pc_can_type_target(&s,21,31,id));g_free(id);
    const char *rejected[]={
        "{\"id\":\"50004:20:30\",\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]}",
        "{\"id\":\"2.a.11\",\"role\":\"button\",\"password\":false,\"bounds\":[20,30,100,50]}",
        "{\"id\":\"2.a.11\",\"role\":\"edit\",\"password\":true,\"bounds\":[20,30,100,50]}",
        "{\"id\":\"2.a.11\",\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,\"100\",50]}",
        "{\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]}",NULL};
    for(guint i=0;rejected[i];i++){char *snapshot=g_strdup_printf("{\"window\":{\"pid\":4},\"controls\":[%s]}",rejected[i]);s.structured=snapshot;g_assert_null(pc_edit_target_id(&s,21,31));g_free(snapshot);}
    s.structured="{\"window\":{\"pid\":4},\"controls\":[]}";g_assert_null(pc_edit_target_id(&s,21,31));
    s.structured="{\"window\":{\"pid\":4},\"controls\":[{\"id\":\"2.a.11\",\"role\":\"edit\",\"password\":false,\"bounds\":[20,30,100,50]},{\"id\":\"2.a.12\",\"role\":\"combobox\",\"password\":false,\"bounds\":[20,30,100,50]}]}";g_assert_null(pc_edit_target_id(&s,21,31));
}
#ifdef G_OS_WIN32
static HANDLE created;static HWND fixture,edit,button,password;static volatile LONG clicked;static gboolean fixture_thin;
static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==WM_COMMAND&&LOWORD(wp)==2){InterlockedIncrement(&clicked);return 0;}if(m==WM_APP+1){SetFocus((HWND)wp);return 0;}if(m==WM_TIMER&&wp==91){KillTimer(w,91);SetWindowTextW(button,L"Target changed during cursor travel");return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,wp,lp);}
static DWORD WINAPI fixture_thread(LPVOID unused){
    (void)unused;WNDCLASSW wc={0};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"BenchDesktopTest";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    fixture=CreateWindowW(wc.lpszClassName,L"Bench desktop control test - disposable",WS_OVERLAPPEDWINDOW,100,100,640,400,NULL,NULL,wc.hInstance,NULL);
    if(!fixture_thin){
        edit=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,30,40,500,50,fixture,(HMENU)1,wc.hInstance,NULL);
        button=CreateWindowW(L"BUTTON",L"Verify click",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,30,140,200,50,fixture,(HMENU)2,wc.hInstance,NULL);
        password=CreateWindowW(L"EDIT",L"password-must-never-leak",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD,30,230,300,40,fixture,(HMENU)3,wc.hInstance,NULL);
    }
    ShowWindow(fixture,SW_SHOW);ShowWindow(fixture,SW_SHOW);SetForegroundWindow(fixture);
    /* Signal readiness after the initial client and child paint, not merely
     * ShowWindow: the observation test must not race an unpainted fixture. */
    UpdateWindow(fixture);RedrawWindow(fixture,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);SetEvent(created);
    MSG m;while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
}
static void target(PcSession *s,HWND child,PcAction *a){RECT r;GetWindowRect(child,&r);a->x=(int)(((r.left+r.right)/2-s->left)*(gint64)s->image_width/s->width);a->y=(int)(((r.top+r.bottom)/2-s->top)*(gint64)s->image_height/s->height);}
static gboolean check_structured(PcSession *s,GError **e){
    SendMessageW(fixture,WM_APP+1,(WPARAM)edit,0);
    if(!pc_observe(s,e))return FALSE;
    g_assert_nonnull(s->structured);g_assert_null(s->image);g_assert_null(s->pixels);g_assert_false(s->structured_thin);g_assert_null(strstr(s->structured,"password-must-never-leak"));
    JsonParser *p=json_parser_new();g_assert_true(json_parser_load_from_data(p,s->structured,-1,NULL));JsonObject *root=json_node_get_object(json_parser_get_root(p));JsonObject *window=json_object_get_object_member(root,"window"),*focused=json_object_get_object_member(root,"focused");
    g_assert_cmpstr(json_object_get_string_member(window,"title"),==,"Bench desktop control test - disposable");g_assert_cmpint(json_object_get_int_member(window,"pid"),==,GetCurrentProcessId());g_assert_nonnull(strstr(json_object_get_string_member(window,"process"),"test-pc"));g_assert_cmpstr(json_object_get_string_member(focused,"role"),==,"edit");
    JsonArray *controls=json_object_get_array_member(root,"controls");g_assert_cmpuint(json_array_get_length(controls),<=,12);gboolean found_button=FALSE,found_password=FALSE;
    for(guint i=0;i<json_array_get_length(controls);i++){JsonObject *control=json_array_get_object_element(controls,i);if(!strcmp(json_object_get_string_member(control,"name"),"Verify click"))found_button=TRUE;if(json_object_get_boolean_member(control,"password")){found_password=TRUE;g_assert_cmpstr(json_object_get_string_member(control,"value"),==,"");}}
    g_assert_true(found_button);g_assert_true(found_password);g_object_unref(p);PcAction at={0};target(s,edit,&at);g_assert_true(pc_can_type_at(s,at.x,at.y));char *edit_id=pc_edit_target_id(s,at.x,at.y);g_assert_nonnull(edit_id);g_assert_true(pc_can_type_target(s,at.x,at.y,edit_id));g_free(edit_id);
    g_assert_true(pc_revalidate(s,e));g_assert_no_error(*e);g_assert_null(s->image);
    /* Explicit screenshots remain supported, but the next structured refresh
     * drops both the attachment and retained pixels rather than reusing them. */
    g_assert_true(pc_capture(s,e));g_assert_nonnull(s->image);g_assert_nonnull(s->pixels);g_assert_true(pc_observe(s,e));g_assert_null(s->image);g_assert_null(s->pixels);
    LONG before=clicked;SetWindowTextW(edit,L"Structured state changed");g_assert_false(pc_revalidate(s,e));g_assert_error(*e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK);g_clear_error(e);g_assert_nonnull(strstr(s->structured,"Structured state changed"));g_assert_cmpint(clicked,==,before);g_assert_null(s->image);g_assert_true(pc_revalidate(s,e));g_assert_no_error(*e);
    SendMessageW(fixture,WM_APP+1,(WPARAM)password,0);g_assert_true(pc_observe(s,e));g_assert_null(strstr(s->structured,"password-must-never-leak"));target(s,password,&at);g_assert_false(pc_can_type_at(s,at.x,at.y));
    /* A provider that never returns is killed by the host deadline. This
     * fault injection lives only in this test binary's worker dispatcher. */
    g_setenv("BENCH_TEST_OBSERVER_HANG","1",TRUE);gint64 began=g_get_monotonic_time();g_assert_true(pc_observe(s,e));g_unsetenv("BENCH_TEST_OBSERVER_HANG");g_assert_true(s->structured_thin);g_assert_null(s->image);g_assert_cmpint(g_get_monotonic_time()-began,<,6*G_USEC_PER_SEC);g_assert_nonnull(strstr(s->structured,"\"truncated\":true"));
    g_assert_false(pc_revalidate(s,e));g_assert_nonnull(*e);g_clear_error(e);
    SetWindowTextW(edit,L"");SendMessageW(fixture,WM_APP+1,(WPARAM)edit,0);return TRUE;
}
static int structured_fixture(void){
    SetProcessDPIAware();created=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE thread=CreateThread(NULL,0,fixture_thread,NULL,0,NULL);WaitForSingleObject(created,5000);
    PcSession s={0};GError *e=NULL;char *id=g_strdup_printf("%"G_GUINT64_FORMAT":%lu",(guint64)(guintptr)fixture,(unsigned long)GetCurrentProcessId());gboolean ok=pc_open(&s,id,&e)&&check_structured(&s,&e);g_free(id);
    g_print("Native structured observation fixture: %s%s%s\n",ok?"PASS":"FAIL",e?" - ":"",e?e->message:"");pc_clear(&s);g_clear_error(&e);PostMessageW(fixture,WM_CLOSE,0,0);WaitForSingleObject(thread,3000);CloseHandle(thread);CloseHandle(created);return ok?0:1;
}
static int interactive(void){
    /* Explicit manual test only. All real input is restricted to this process's
     * disposable fixture window. It never runs in make check or GitHub CI. */
    SetProcessDPIAware();created=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE thread=CreateThread(NULL,0,fixture_thread,NULL,0,NULL);WaitForSingleObject(created,5000);
    PcSession s={0};GError *e=NULL;char *id=g_strdup_printf("%"G_GUINT64_FORMAT":%lu",(guint64)(guintptr)fixture,(unsigned long)GetCurrentProcessId());gboolean ok=pc_open(&s,id,&e)&&pc_capture(&s,&e);g_free(id);
    if(ok){
        guchar *before=g_memdup2(s.pixels,(gsize)s.image_width*s.image_height*3);
        int cx=s.client_x,cy=s.client_y,cw=s.client_width,ch=s.client_height,iw=s.image_width;
        ok=pc_revalidate(&s,&e);
        if(!ok&&s.image_width==iw&&s.client_x==cx&&s.client_y==cy&&s.client_width==cw&&s.client_height==ch){int minx=iw,miny=s.image_height,maxx=0,maxy=0;guint changed=0;
            for(int y=cy;y<cy+ch;y++)for(int x=cx;x<cx+cw;x++){gsize p=((gsize)y*iw+x)*3;if(ABS((int)before[p]-s.pixels[p])>24||ABS((int)before[p+1]-s.pixels[p+1])>24||ABS((int)before[p+2]-s.pixels[p+2])>24){changed++;minx=MIN(minx,x);miny=MIN(miny,y);maxx=MAX(maxx,x);maxy=MAX(maxy,y);}}
            g_printerr("Initial frame diff: %u pixels; bbox %d,%d-%d,%d; client %d,%d %dx%d\n",changed,minx,miny,maxx,maxy,cx,cy,cw,ch);
        }
        g_free(before);
    }
    PcAction a={.kind="click"};if(ok){target(&s,edit,&a);ok=pc_execute(&s,&a,&e);if(!ok&&g_error_matches(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK)){g_clear_error(&e);ok=pc_execute(&s,&a,&e);}}a.kind="type";a.text="Bench desktop verified";if(ok)ok=pc_execute(&s,&a,&e);Sleep(200);wchar_t value[100]={0};SendMessageW(edit,WM_GETTEXT,100,(LPARAM)value);if(ok&&wcscmp(value,L"Bench desktop verified")){char *seen=g_utf16_to_utf8((gunichar2*)value,-1,NULL,NULL,NULL);g_printerr("Typing mismatch: %s\n",seen);g_free(seen);ok=FALSE;}
    a.kind="key";a.text="Home";if(ok)ok=pc_execute(&s,&a,&e);a.kind="type";a.text="Unicode: caf\303\251";if(ok)ok=pc_execute(&s,&a,&e);Sleep(200);SendMessageW(edit,WM_GETTEXT,100,(LPARAM)value);if(ok&&wcscmp(value,L"Unicode: caf\u00e9Bench desktop verified")){char *seen=g_utf16_to_utf8((gunichar2*)value,-1,NULL,NULL,NULL);g_printerr("Unicode mismatch: %s\n",seen);g_free(seen);ok=FALSE;}
    if(ok){
        /* Change the semantic target after preflight, during the 1s cursor
         * animation. The final observation must reject before mouse-down. */
        ok=pc_observe(&s,&e);if(ok){LONG before=clicked;a.kind="click";target(&s,button,&a);g_assert_cmpuint(SetTimer(fixture,91,250,NULL),!=,0);g_assert_false(pc_execute(&s,&a,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK);g_clear_error(&e);g_assert_cmpint(clicked,==,before);g_assert_nonnull(strstr(s.structured,"Target changed during cursor travel"));}
    }
    a.kind="click";if(ok){
        /* Typing changed the old frame; observe before proposing the next
         * click. Native hover decoration may require one fresh-frame retry. */
        ok=pc_capture(&s,&e);if(ok){target(&s,button,&a);ok=pc_execute(&s,&a,&e);if(!ok&&g_error_matches(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK)){g_clear_error(&e);ok=pc_execute(&s,&a,&e);}}
    }Sleep(200);if(ok&&!clicked){g_printerr("Click was not received\n");ok=FALSE;}
    if(ok)ok=pc_capture(&s,&e);
    if(ok&&!g_str_has_prefix(s.image,"data:image/png;base64,iVBOR"))ok=FALSE;
    if(ok){
        LONG before=clicked;char *previous=g_strdup(s.image);gint64 old_time=s.captured_us;
        SetWindowTextW(edit,L"The screen changed while the approval was pending. Do not click the old target.");
        RedrawWindow(fixture,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        g_assert_false(pc_revalidate(&s,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK);g_clear_error(&e);
        g_assert_cmpstr(s.image,!=,previous);g_assert_cmpint(s.captured_us,>,old_time);g_assert_cmpint(clicked,==,before);g_free(previous);
        /* The fresh observation replaces the stale frame and can be used for
         * a new proposal; pc_revalidate itself must never click or type. */
        g_assert_true(pc_revalidate(&s,&e));g_assert_no_error(e);
        s.captured_us-=PC_OBSERVATION_MAX_AGE_US+G_USEC_PER_SEC;
        g_assert_false(pc_revalidate(&s,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK);g_clear_error(&e);
        g_assert_cmpint(g_get_monotonic_time()-s.captured_us,<,G_USEC_PER_SEC);g_assert_cmpint(clicked,==,before);
    }
    if(ok){
        int old_left=s.left;MoveWindow(fixture,120,120,640,400,TRUE);
        g_assert_false(pc_execute(&s,&a,&e));g_assert_nonnull(e);g_clear_error(&e);
        g_assert_false(pc_revalidate(&s,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_WOULD_BLOCK);g_clear_error(&e);g_assert_cmpint(s.left,!=,old_left);
    }
    if(ok){INPUT stop[8]={{0}};WORD keys[]={VK_CONTROL,VK_MENU,VK_SHIFT,VK_F10};for(int i=0;i<4;i++){stop[i].type=stop[7-i].type=INPUT_KEYBOARD;stop[i].ki.wVk=stop[7-i].ki.wVk=keys[i];stop[7-i].ki.dwFlags=KEYEVENTF_KEYUP;}ok=SendInput(8,stop,sizeof(INPUT))==8;Sleep(100);if(ok)ok=pc_cancelled();if(!ok)g_printerr("Emergency stop was not received\n");}
    g_print("Native desktop fixture: %s%s%s\n",ok?"PASS":"FAIL",e?" - ":"",e?e->message:"");pc_clear(&s);g_clear_error(&e);PostMessageW(fixture,WM_CLOSE,0,0);WaitForSingleObject(thread,3000);CloseHandle(thread);CloseHandle(created);return ok?0:1;
}
#endif
int main(int argc,char **argv){
    if(argc==3&&!strcmp(argv[1],"--desktop-observe")){
#ifdef G_OS_WIN32
        if(g_getenv("BENCH_TEST_OBSERVER_HANG"))Sleep(10000);
#endif
        return pc_observe_worker(argv[2]);
    }
#ifdef G_OS_WIN32
    if(argc==2&&!strcmp(argv[1],"--structured"))return structured_fixture();
    if(argc==2&&!strcmp(argv[1],"--interactive"))return interactive();
    if(argc==2&&(!strcmp(argv[1],"--fixture")||!strcmp(argv[1],"--thin-fixture"))){
        fixture_thin=!strcmp(argv[1],"--thin-fixture");
        SetProcessDPIAware();created=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE thread=CreateThread(NULL,0,fixture_thread,NULL,0,NULL);WaitForSingleObject(created,5000);
        g_print("%"G_GUINT64_FORMAT":%lu\n",(guint64)(guintptr)fixture,(unsigned long)GetCurrentProcessId());fflush(stdout);
        WaitForSingleObject(thread,300000);PostMessageW(fixture,WM_CLOSE,0,0);WaitForSingleObject(thread,3000);CloseHandle(thread);CloseHandle(created);return 0;
    }
#endif
    g_test_init(&argc,&argv,NULL);g_test_add_func("/desktop/parser",parser);g_test_add_func("/desktop/vision-request",request);g_test_add_func("/desktop/no-capture-no-input",no_capture);g_test_add_func("/desktop/structured-compare",structured_compare);g_test_add_func("/desktop/typing-guard",typing_guard);g_test_add_func("/desktop/typing-identity",typing_identity);return g_test_run();
}
