#include "pc_control.h"
#include <gio/gio.h>
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
static void no_capture(void){PcSession s={0};PcAction a={.kind="click",.x=0,.y=0};GError *e=NULL;g_assert_false(pc_execute(&s,&a,&e));g_assert_nonnull(e);g_clear_error(&e);}
#ifdef G_OS_WIN32
static HANDLE created;static HWND fixture,edit,button;static volatile LONG clicked;
static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==WM_COMMAND&&LOWORD(wp)==2){InterlockedIncrement(&clicked);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,wp,lp);}
static DWORD WINAPI fixture_thread(LPVOID unused){(void)unused;WNDCLASSW wc={0};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"BenchDesktopTest";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);fixture=CreateWindowW(wc.lpszClassName,L"Bench desktop control test - disposable",WS_OVERLAPPEDWINDOW,100,100,640,400,NULL,NULL,wc.hInstance,NULL);edit=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,30,40,500,50,fixture,(HMENU)1,wc.hInstance,NULL);button=CreateWindowW(L"BUTTON",L"Verify click",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,30,140,200,50,fixture,(HMENU)2,wc.hInstance,NULL);ShowWindow(fixture,SW_SHOW);ShowWindow(fixture,SW_SHOW);SetForegroundWindow(fixture);SetEvent(created);MSG m;while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;}
static void target(PcSession *s,HWND child,PcAction *a){RECT r;GetWindowRect(child,&r);a->x=(int)(((r.left+r.right)/2-s->left)*(gint64)s->image_width/s->width);a->y=(int)(((r.top+r.bottom)/2-s->top)*(gint64)s->image_height/s->height);}
static int interactive(void){
    /* Explicit manual test only. All real input is restricted to this process's
     * disposable fixture window. It never runs in make check or GitHub CI. */
    SetProcessDPIAware();created=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE thread=CreateThread(NULL,0,fixture_thread,NULL,0,NULL);WaitForSingleObject(created,5000);
    PcSession s={0};GError *e=NULL;char *id=g_strdup_printf("%"G_GUINT64_FORMAT":%lu",(guint64)(guintptr)fixture,(unsigned long)GetCurrentProcessId());gboolean ok=pc_open(&s,id,&e)&&pc_capture(&s,&e);g_free(id);
    PcAction a={.kind="click"};if(ok){target(&s,edit,&a);ok=pc_execute(&s,&a,&e);}a.kind="type";a.text="Bench desktop verified";if(ok)ok=pc_execute(&s,&a,&e);Sleep(200);wchar_t value[100]={0};SendMessageW(edit,WM_GETTEXT,100,(LPARAM)value);if(ok&&wcscmp(value,L"Bench desktop verified")){char *seen=g_utf16_to_utf8((gunichar2*)value,-1,NULL,NULL,NULL);g_printerr("Typing mismatch: %s\n",seen);g_free(seen);ok=FALSE;}
    a.kind="key";a.text="Home";if(ok)ok=pc_execute(&s,&a,&e);a.kind="type";a.text="Unicode: caf\303\251";if(ok)ok=pc_execute(&s,&a,&e);Sleep(200);SendMessageW(edit,WM_GETTEXT,100,(LPARAM)value);if(ok&&wcscmp(value,L"Unicode: caf\u00e9Bench desktop verified")){char *seen=g_utf16_to_utf8((gunichar2*)value,-1,NULL,NULL,NULL);g_printerr("Unicode mismatch: %s\n",seen);g_free(seen);ok=FALSE;}
    a.kind="click";if(ok){target(&s,button,&a);ok=pc_execute(&s,&a,&e);}Sleep(200);if(ok&&!clicked){g_printerr("Click was not received\n");ok=FALSE;}
    if(ok)ok=pc_capture(&s,&e);
    if(ok&&!g_str_has_prefix(s.image,"data:image/png;base64,iVBOR"))ok=FALSE;
    if(ok){MoveWindow(fixture,120,120,640,400,TRUE);g_assert_false(pc_execute(&s,&a,&e));g_assert_nonnull(e);g_clear_error(&e);}
    if(ok){INPUT stop[8]={{0}};WORD keys[]={VK_CONTROL,VK_MENU,VK_SHIFT,VK_F10};for(int i=0;i<4;i++){stop[i].type=stop[7-i].type=INPUT_KEYBOARD;stop[i].ki.wVk=stop[7-i].ki.wVk=keys[i];stop[7-i].ki.dwFlags=KEYEVENTF_KEYUP;}ok=SendInput(8,stop,sizeof(INPUT))==8;Sleep(100);if(ok)ok=pc_cancelled();if(!ok)g_printerr("Emergency stop was not received\n");}
    g_print("Native desktop fixture: %s%s%s\n",ok?"PASS":"FAIL",e?" - ":"",e?e->message:"");pc_clear(&s);g_clear_error(&e);PostMessageW(fixture,WM_CLOSE,0,0);WaitForSingleObject(thread,3000);CloseHandle(thread);CloseHandle(created);return ok?0:1;
}
#endif
int main(int argc,char **argv){
#ifdef G_OS_WIN32
    if(argc==2&&!strcmp(argv[1],"--interactive"))return interactive();
    if(argc==2&&!strcmp(argv[1],"--fixture")){
        SetProcessDPIAware();created=CreateEventW(NULL,TRUE,FALSE,NULL);HANDLE thread=CreateThread(NULL,0,fixture_thread,NULL,0,NULL);WaitForSingleObject(created,5000);
        g_print("%"G_GUINT64_FORMAT":%lu\n",(guint64)(guintptr)fixture,(unsigned long)GetCurrentProcessId());fflush(stdout);
        WaitForSingleObject(thread,300000);PostMessageW(fixture,WM_CLOSE,0,0);WaitForSingleObject(thread,3000);CloseHandle(thread);CloseHandle(created);return 0;
    }
#endif
    g_test_init(&argc,&argv,NULL);g_test_add_func("/desktop/parser",parser);g_test_add_func("/desktop/vision-request",request);g_test_add_func("/desktop/no-capture-no-input",no_capture);return g_test_run();
}
