/* Bounded, read-only Windows UI Automation client. Provider code lives in a
 * disposable MTA helper process, so an inaccessible/hung app cannot hang the
 * engine or its emergency stop. The helper has no screenshot/input path. */
#include "pc_observe.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>

#define OBSERVE_TIMEOUT_MS 3000
#define OBSERVE_MAX_BYTES 32768
#define OBSERVE_MAX_NODES 128
#define OBSERVE_MAX_CONTROLS 12

static gboolean observation_error(GError **error,const char *message){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,message);return FALSE;}
static JsonArray *numbers(int x,int y,int w,int h,gboolean rectangle){JsonArray *a=json_array_new();json_array_add_int_element(a,x);json_array_add_int_element(a,y);if(rectangle){json_array_add_int_element(a,w);json_array_add_int_element(a,h);}return a;}
static char *serialize(JsonObject *o){JsonNode *n=json_node_new(JSON_NODE_OBJECT);json_node_set_object(n,o);JsonGenerator *g=json_generator_new();json_generator_set_root(g,n);char *s=json_generator_to_data(g,NULL);g_object_unref(g);json_node_free(n);return s;}
static char *short_text(const wchar_t *s,gsize limit,gboolean *truncated){char *utf=s?g_utf16_to_utf8((const gunichar2*)s,-1,NULL,NULL,NULL):NULL;if(!utf)return g_strdup("");if(g_utf8_strlen(utf,-1)>(glong)limit){*g_utf8_offset_to_pointer(utf,limit)=0;if(truncated)*truncated=TRUE;}return utf;}
/* The geometry is identical to pc_capture/pc_execute: physical clipped window,
 * with control bounds/cursor relative to a <=1600px logical viewport. */
static gboolean geometry(PcSession *s,GError **error){
    HWND hwnd=(HWND)(guintptr)s->window;DWORD pid=0;RECT r,client;POINT origin={0,0};GetWindowThreadProcessId(hwnd,&pid);
    if(!IsWindow(hwnd)||!IsWindowVisible(hwnd)||pid!=s->pid||GetForegroundWindow()!=hwnd)return observation_error(error,"Selected app closed, changed process, or lost focus during observation.");
    if(!GetWindowRect(hwnd,&r)||!GetClientRect(hwnd,&client)||!ClientToScreen(hwnd,&origin))return observation_error(error,"Cannot read selected app geometry.");
    int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);r.left=MAX(r.left,vx);r.top=MAX(r.top,vy);r.right=MIN(r.right,vx+GetSystemMetrics(SM_CXVIRTUALSCREEN));r.bottom=MIN(r.bottom,vy+GetSystemMetrics(SM_CYVIRTUALSCREEN));
    int w=r.right-r.left,h=r.bottom-r.top;if(w<2||h<2||w>16000||h>16000)return observation_error(error,"Selected app is off-screen or has unsupported bounds.");
    double scale=MIN(1.0,1600.0/MAX(w,h));s->left=r.left;s->top=r.top;s->width=w;s->height=h;s->image_width=MAX(1,(int)(w*scale));s->image_height=MAX(1,(int)(h*scale));
    s->client_x=(int)((gint64)(CLAMP(origin.x,r.left,r.right)-r.left)*s->image_width/w);s->client_y=(int)((gint64)(CLAMP(origin.y,r.top,r.bottom)-r.top)*s->image_height/h);
    s->client_width=(int)((gint64)(CLAMP(origin.x+client.right,r.left,r.right)-r.left)*s->image_width/w)-s->client_x;s->client_height=(int)((gint64)(CLAMP(origin.y+client.bottom,r.top,r.bottom)-r.top)*s->image_height/h)-s->client_y;
    return s->client_width>0&&s->client_height>0?TRUE:observation_error(error,"Selected app client area is unavailable.");
}
static JsonObject *basic_observation(PcSession *s){
    wchar_t title[512]={0},process[1024]={0};GetWindowTextW((HWND)(guintptr)s->window,title,G_N_ELEMENTS(title));
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,s->pid);DWORD length=G_N_ELEMENTS(process);if(p){if(!QueryFullProcessImageNameW(p,0,process,&length))process[0]=0;CloseHandle(p);}
    wchar_t *base=wcsrchr(process,L'\\');char *name=short_text(base?base+1:process,160,NULL),*caption=short_text(title,160,NULL);
    JsonObject *o=json_object_new(),*window=json_object_new();json_object_set_string_member(window,"title",caption);json_object_set_string_member(window,"process",name);json_object_set_int_member(window,"pid",s->pid);json_object_set_array_member(window,"bounds",numbers(s->left,s->top,s->width,s->height,TRUE));json_object_set_object_member(o,"window",window);g_free(name);g_free(caption);
    json_object_set_null_member(o,"focused");json_object_set_array_member(o,"controls",json_array_new());POINT cursor={0,0};GetCursorPos(&cursor);json_object_set_array_member(o,"cursor",numbers((int)((gint64)(cursor.x-s->left)*s->image_width/s->width),(int)((gint64)(cursor.y-s->top)*s->image_height/s->height),0,0,FALSE));json_object_set_array_member(o,"viewport",numbers(s->image_width,s->image_height,0,0,FALSE));json_object_set_boolean_member(o,"thin",TRUE);json_object_set_boolean_member(o,"truncated",FALSE);return o;
}
static const char *role(CONTROLTYPEID type){switch(type){case UIA_ButtonControlTypeId:return "button";case UIA_EditControlTypeId:return "edit";case UIA_HyperlinkControlTypeId:return "link";case UIA_ComboBoxControlTypeId:return "combobox";case UIA_CheckBoxControlTypeId:return "checkbox";case UIA_RadioButtonControlTypeId:return "radio";case UIA_TabItemControlTypeId:return "tab";case UIA_MenuItemControlTypeId:return "menuitem";case UIA_TextControlTypeId:return "text";case UIA_DocumentControlTypeId:return "document";case UIA_WindowControlTypeId:return "window";case UIA_PaneControlTypeId:return "pane";default:return "control";}}
static gboolean actionable(CONTROLTYPEID type){return type==UIA_ButtonControlTypeId||type==UIA_EditControlTypeId||type==UIA_HyperlinkControlTypeId||type==UIA_ComboBoxControlTypeId||type==UIA_CheckBoxControlTypeId||type==UIA_RadioButtonControlTypeId||type==UIA_TabItemControlTypeId||type==UIA_MenuItemControlTypeId;}
static JsonObject *control(IUIAutomationElement *element,PcSession *s,gboolean only_actionable,gboolean *truncated){
    CONTROLTYPEID type=0;WINBOOL offscreen=TRUE,enabled=FALSE,password=TRUE;RECT r={0};
    if(FAILED(IUIAutomationElement_get_CurrentControlType(element,&type))||(only_actionable&&!actionable(type)))return NULL;
    if(FAILED(IUIAutomationElement_get_CurrentBoundingRectangle(element,&r))||FAILED(IUIAutomationElement_get_CurrentIsOffscreen(element,&offscreen))||offscreen)return NULL;
    if(only_actionable&&(FAILED(IUIAutomationElement_get_CurrentIsEnabled(element,&enabled))||!enabled))return NULL;
    r.left=MAX(r.left,s->left);r.top=MAX(r.top,s->top);r.right=MIN(r.right,s->left+s->width);r.bottom=MIN(r.bottom,s->top+s->height);if(r.right<=r.left||r.bottom<=r.top)return NULL;
    int x=(int)((gint64)(r.left-s->left)*s->image_width/s->width),y=(int)((gint64)(r.top-s->top)*s->image_height/s->height),right=(int)((gint64)(r.right-s->left)*s->image_width/s->width),bottom=(int)((gint64)(r.bottom-s->top)*s->image_height/s->height);
    /* Standard title-bar buttons do not make an otherwise opaque canvas a
     * useful structured observation. Only actionable client content counts. */
    if(only_actionable&&(right<=s->client_x||bottom<=s->client_y||x>=s->client_x+s->client_width||y>=s->client_y+s->client_height))return NULL;
    BSTR raw_name=NULL;IUIAutomationElement_get_CurrentName(element,&raw_name);char *name=short_text(raw_name,160,truncated);SysFreeString(raw_name);
    /* A failed password-property read is treated as protected. Never query its
     * Value/Text pattern in that case, including for nearby controls. */
    if(FAILED(IUIAutomationElement_get_CurrentIsPassword(element,&password)))password=TRUE;
    char *value=g_strdup("");if(!password){VARIANT v;VariantInit(&v);gboolean has_value=FALSE;if(SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(element,UIA_ValueValuePropertyId,&v))&&V_VT(&v)==VT_BSTR){g_free(value);value=short_text(V_BSTR(&v),256,truncated);has_value=TRUE;}VariantClear(&v);
        /* Documents often support TextPattern rather than ValuePattern. Read
         * only the focused document's bounded prefix, never whole documents
         * or text from nearby controls. The disposable worker bounds hangs. */
        if(!only_actionable&&!has_value){IUIAutomationTextPattern *pattern=NULL;IUIAutomationTextRange *range=NULL;BSTR text=NULL;
            if(SUCCEEDED(IUIAutomationElement_GetCurrentPatternAs(element,UIA_TextPatternId,&IID_IUIAutomationTextPattern,(void**)&pattern))&&pattern&&SUCCEEDED(IUIAutomationTextPattern_get_DocumentRange(pattern,&range))&&range&&SUCCEEDED(IUIAutomationTextRange_GetText(range,257,&text))&&text){g_free(value);value=short_text(text,256,truncated);}
            SysFreeString(text);if(range)IUIAutomationTextRange_Release(range);if(pattern)IUIAutomationTextPattern_Release(pattern);
        }
    }
    SAFEARRAY *runtime=NULL;GString *id=g_string_new(NULL);if(SUCCEEDED(IUIAutomationElement_GetRuntimeId(element,&runtime))&&runtime){LONG low=0,high=-1;if(SUCCEEDED(SafeArrayGetLBound(runtime,1,&low))&&SUCCEEDED(SafeArrayGetUBound(runtime,1,&high)))for(LONG i=low;i<=high&&i<low+12;i++){int part=0;if(SUCCEEDED(SafeArrayGetElement(runtime,&i,&part)))g_string_append_printf(id,"%s%x",id->len?".":"",(unsigned)part);}SafeArrayDestroy(runtime);}
    if(!id->len)g_string_printf(id,"%d:%d:%d",type,x,y);
    JsonObject *o=json_object_new();json_object_set_string_member(o,"id",id->str);json_object_set_string_member(o,"role",role(type));json_object_set_string_member(o,"name",name);json_object_set_string_member(o,"value",value);json_object_set_array_member(o,"bounds",numbers(x,y,MAX(1,right-x),MAX(1,bottom-y),TRUE));json_object_set_boolean_member(o,"password",password!=FALSE);g_string_free(id,TRUE);g_free(name);g_free(value);return o;
}
typedef struct { PcSession *session; IUIAutomationTreeWalker *walker; GPtrArray *controls; guint visited; gboolean truncated; } Walk;
static void walk_children(Walk *w,IUIAutomationElement *parent,guint depth){
    if(depth>=8||w->visited>=OBSERVE_MAX_NODES){w->truncated=TRUE;return;}
    IUIAutomationElement *child=NULL;if(FAILED(IUIAutomationTreeWalker_GetFirstChildElement(w->walker,parent,&child)))return;
    while(child){if(w->visited++>=OBSERVE_MAX_NODES){w->truncated=TRUE;IUIAutomationElement_Release(child);break;}JsonObject *item=control(child,w->session,TRUE,&w->truncated);if(item)g_ptr_array_add(w->controls,item);walk_children(w,child,depth+1);IUIAutomationElement *next=NULL;if(w->visited<OBSERVE_MAX_NODES)IUIAutomationTreeWalker_GetNextSiblingElement(w->walker,child,&next);else w->truncated=TRUE;IUIAutomationElement_Release(child);child=next;}
}
static gboolean in_window(IUIAutomation *automation,IUIAutomationTreeWalker *walker,IUIAutomationElement *root,IUIAutomationElement *element){
    IUIAutomationElement_AddRef(element);IUIAutomationElement *current=element;gboolean found=FALSE;for(int depth=0;current&&depth<32;depth++){WINBOOL same=FALSE;if(SUCCEEDED(IUIAutomation_CompareElements(automation,root,current,&same))&&same){found=TRUE;IUIAutomationElement_Release(current);current=NULL;break;}IUIAutomationElement *parent=NULL;IUIAutomationTreeWalker_GetParentElement(walker,current,&parent);IUIAutomationElement_Release(current);current=parent;}if(current)IUIAutomationElement_Release(current);return found;
}
static gint nearest(gconstpointer pa,gconstpointer pb,gpointer data){JsonObject *a=*(JsonObject*const*)pa,*b=*(JsonObject*const*)pb;POINT *point=data;JsonArray *ra=json_object_get_array_member(a,"bounds"),*rb=json_object_get_array_member(b,"bounds");gint64 ax=json_array_get_int_element(ra,0)+json_array_get_int_element(ra,2)/2-point->x,ay=json_array_get_int_element(ra,1)+json_array_get_int_element(ra,3)/2-point->y,bx=json_array_get_int_element(rb,0)+json_array_get_int_element(rb,2)/2-point->x,by=json_array_get_int_element(rb,1)+json_array_get_int_element(rb,3)/2-point->y;gint64 da=ax*ax+ay*ay,db=bx*bx+by*by;return da<db?-1:da>db?1:strcmp(json_object_get_string_member(a,"id"),json_object_get_string_member(b,"id"));}
static void enrich(JsonObject *o,PcSession *s){
    json_object_set_boolean_member(o,"truncated",TRUE);
    HRESULT init=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(init))return;
    IUIAutomation *automation=NULL;IUIAutomationElement *root=NULL,*focused=NULL;IUIAutomationTreeWalker *walker=NULL;
    if(FAILED(CoCreateInstance(&CLSID_CUIAutomation,NULL,CLSCTX_INPROC_SERVER,&IID_IUIAutomation,(void**)&automation)))goto done;
    if(FAILED(IUIAutomation_ElementFromHandle(automation,(HWND)(guintptr)s->window,&root))||!root||FAILED(IUIAutomation_get_ControlViewWalker(automation,&walker))||!walker)goto done;
    Walk w={.session=s,.walker=walker,.controls=g_ptr_array_new_with_free_func((GDestroyNotify)json_object_unref)};
    JsonObject *focus_object=NULL;if(SUCCEEDED(IUIAutomation_GetFocusedElement(automation,&focused))&&focused&&in_window(automation,walker,root,focused))focus_object=control(focused,s,FALSE,&w.truncated);
    if(focus_object)json_object_set_object_member(o,"focused",focus_object);
    walk_children(&w,root,0);
    POINT center={s->image_width/2,s->image_height/2};if(focus_object){JsonArray *b=json_object_get_array_member(focus_object,"bounds");center.x=json_array_get_int_element(b,0)+json_array_get_int_element(b,2)/2;center.y=json_array_get_int_element(b,1)+json_array_get_int_element(b,3)/2;}
    /* Focus-centered ordering is stable when the cursor moves during approval. */
    g_ptr_array_sort_with_data(w.controls,nearest,&center);JsonArray *controls=json_object_get_array_member(o,"controls");for(guint i=0;i<MIN(w.controls->len,OBSERVE_MAX_CONTROLS);i++)json_array_add_object_element(controls,json_object_ref(g_ptr_array_index(w.controls,i)));
    gboolean empty_document=focus_object&&!strcmp(json_object_get_string_member(focus_object,"role"),"document")&&!*json_object_get_string_member(focus_object,"value");
    json_object_set_boolean_member(o,"thin",w.controls->len==0||empty_document);json_object_set_boolean_member(o,"truncated",w.truncated||w.controls->len>OBSERVE_MAX_CONTROLS);g_ptr_array_unref(w.controls);
done:if(focused)IUIAutomationElement_Release(focused);if(walker)IUIAutomationTreeWalker_Release(walker);if(root)IUIAutomationElement_Release(root);if(automation)IUIAutomation_Release(automation);CoUninitialize();
}
int pc_observe_worker(const char *id){
    char *end=NULL,*pid_end=NULL;PcSession s={0};s.window=id?g_ascii_strtoull(id,&end,10):0;if(!s.window||!end||*end!=':')return 2;guint64 pid=g_ascii_strtoull(end+1,&pid_end,10);if(!pid||pid>G_MAXUINT32||!pid_end||*pid_end)return 2;s.pid=(guint32)pid;SetProcessDPIAware();if(!geometry(&s,NULL))return 3;JsonObject *o=basic_observation(&s);enrich(o,&s);PcSession after=s;if(!geometry(&after,NULL)||after.left!=s.left||after.top!=s.top||after.width!=s.width||after.height!=s.height){json_object_unref(o);return 3;}char *json=serialize(o);json_object_unref(o);g_print("%s\n",json);g_free(json);return 0;
}
/* The only inherited handles are anonymous stdout/stderr and NUL stdin. A
 * kill-on-close job also covers cancellation/parent teardown. */
static char *helper(PcSession *s){
    wchar_t exe[32768],command[33000];DWORD length=GetModuleFileNameW(NULL,exe,G_N_ELEMENTS(exe));if(!length||length>=G_N_ELEMENTS(exe))return NULL;
    _snwprintf(command,G_N_ELEMENTS(command),L"\"%ls\" --desktop-observe %llu:%lu",exe,(unsigned long long)s->window,(unsigned long)s->pid);
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE read_pipe=NULL,write_pipe=NULL,input=NULL,job=NULL;PROCESS_INFORMATION pi={0};char *answer=NULL;GString *output=g_string_sized_new(4096);
    if(!CreatePipe(&read_pipe,&write_pipe,&sa,0)||!SetHandleInformation(read_pipe,HANDLE_FLAG_INHERIT,0))goto done;
    input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);if(input==INVALID_HANDLE_VALUE){input=NULL;goto done;}
    job=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))goto done;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(NULL,1,0,&bytes);LPPROC_THREAD_ATTRIBUTE_LIST attrs=g_malloc0(bytes);if(!InitializeProcThreadAttributeList(attrs,1,0,&bytes)){g_free(attrs);goto done;}HANDLE handles[]={input,write_pipe};if(!UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),NULL,NULL)){DeleteProcThreadAttributeList(attrs);g_free(attrs);goto done;}
    STARTUPINFOEXW startup={0};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;startup.StartupInfo.hStdInput=input;startup.StartupInfo.hStdOutput=write_pipe;startup.StartupInfo.hStdError=write_pipe;startup.lpAttributeList=attrs;
    BOOL started=CreateProcessW(exe,command,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,NULL,NULL,&startup.StartupInfo,&pi);DeleteProcThreadAttributeList(attrs);g_free(attrs);if(!started)goto done;if(!AssignProcessToJobObject(job,pi.hProcess))goto done;ResumeThread(pi.hThread);CloseHandle(write_pipe);write_pipe=NULL;
    gint64 deadline=g_get_monotonic_time()+OBSERVE_TIMEOUT_MS*1000;gboolean complete=FALSE;
    while(g_get_monotonic_time()<deadline&&!pc_cancelled()){
        DWORD available=0;while(PeekNamedPipe(read_pipe,NULL,0,NULL,&available,NULL)&&available){char buffer[4096];DWORD got=0;if(!ReadFile(read_pipe,buffer,MIN(available,sizeof(buffer)),&got,NULL)||!got)break;if(output->len+got>OBSERVE_MAX_BYTES)goto done;g_string_append_len(output,buffer,got);}
        if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0){DWORD exit_code=1;GetExitCodeProcess(pi.hProcess,&exit_code);if(!complete){complete=TRUE;continue;}if(exit_code==0&&output->len)answer=g_strdup(output->str);break;}Sleep(10);
    }
done:if(pi.hProcess){if(WaitForSingleObject(pi.hProcess,0)!=WAIT_OBJECT_0)TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,1000);CloseHandle(pi.hProcess);}if(pi.hThread)CloseHandle(pi.hThread);if(job)CloseHandle(job);if(input)CloseHandle(input);if(read_pipe)CloseHandle(read_pipe);if(write_pipe)CloseHandle(write_pipe);g_string_free(output,TRUE);return answer;
}
gboolean pc_observe_window(PcSession *s,GError **error){
    PcSession fresh=*s;if(!geometry(&fresh,error))return FALSE;JsonObject *o=basic_observation(&fresh);json_object_set_boolean_member(o,"truncated",TRUE);char *raw=helper(&fresh);
    if(pc_cancelled()){g_free(raw);json_object_unref(o);return observation_error(error,"Desktop observation cancelled.");}
    PcSession after=fresh;if(!geometry(&after,error)||after.left!=fresh.left||after.top!=fresh.top||after.width!=fresh.width||after.height!=fresh.height){g_free(raw);json_object_unref(o);if(!error||!*error)observation_error(error,"Selected app moved during observation.");return FALSE;}
    if(raw){JsonParser *parser=json_parser_new();if(json_parser_load_from_data(parser,raw,-1,NULL)&&JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))){JsonObject *candidate=json_node_get_object(json_parser_get_root(parser));JsonObject *window=json_object_has_member(candidate,"window")?json_object_get_object_member(candidate,"window"):NULL;JsonArray *bounds=window&&json_object_has_member(window,"bounds")?json_object_get_array_member(window,"bounds"):NULL;
        if(bounds&&json_array_get_length(bounds)==4&&json_object_get_int_member(window,"pid")==s->pid&&json_array_get_int_element(bounds,0)==fresh.left&&json_array_get_int_element(bounds,1)==fresh.top&&json_array_get_int_element(bounds,2)==fresh.width&&json_array_get_int_element(bounds,3)==fresh.height&&json_object_has_member(candidate,"focused")&&json_object_has_member(candidate,"controls")&&json_object_has_member(candidate,"thin")&&json_object_has_member(candidate,"truncated")){const char *keys[]={"focused","controls","thin","truncated"};for(guint i=0;i<G_N_ELEMENTS(keys);i++)json_object_set_member(o,keys[i],json_node_copy(json_object_get_member(candidate,keys[i])));}}
        g_object_unref(parser);g_free(raw);}
    fresh.structured=serialize(o);fresh.structured_thin=json_object_get_boolean_member(o,"thin");json_object_unref(o);fresh.image=NULL;fresh.pixels=NULL;fresh.captured_us=g_get_monotonic_time();g_free(s->structured);g_free(s->image);g_free(s->pixels);*s=fresh;return TRUE;
}
#else
int pc_observe_worker(const char *id){(void)id;return 2;}
gboolean pc_observe_window(PcSession *s,GError **e){(void)s;g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Structured desktop observation currently requires Windows.");return FALSE;}
#endif

gboolean pc_observation_equal(const char *a,const char *b){
    if(!a||!b)return FALSE;
    JsonParser *pa=json_parser_new(),*pb=json_parser_new();gboolean equal=FALSE;
    if(json_parser_load_from_data(pa,a,-1,NULL)&&json_parser_load_from_data(pb,b,-1,NULL)&&JSON_NODE_HOLDS_OBJECT(json_parser_get_root(pa))&&JSON_NODE_HOLDS_OBJECT(json_parser_get_root(pb))){JsonObject *oa=json_node_get_object(json_parser_get_root(pa)),*ob=json_node_get_object(json_parser_get_root(pb));json_object_remove_member(oa,"cursor");json_object_remove_member(ob,"cursor");equal=json_node_equal(json_parser_get_root(pa),json_parser_get_root(pb));}
    g_object_unref(pa);g_object_unref(pb);return equal;
}
