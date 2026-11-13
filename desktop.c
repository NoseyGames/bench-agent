/* Bench's Windows GUI-subsystem entry point. The C engine and supplied web UI
 * stay separate processes. Use the app's Quit button to stop its local host. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wchar.h>
#include <stdio.h>

#define CAP 32768
static int fail(const wchar_t *text) {
    MessageBoxW(NULL,text,L"Bench",MB_OK|MB_ICONERROR);
    return 1;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR args,int show) {
    (void)instance; (void)previous; (void)args; (void)show;
    wchar_t root[CAP],node[CAP],script[CAP],cert[CAP],command[CAP];
    DWORD n=GetModuleFileNameW(NULL,root,CAP);
    if(!n||n>=CAP)return fail(L"The application path is too long.");
    wchar_t *slash=wcsrchr(root,L'\\');
    if(!slash)return fail(L"Cannot locate the Bench folder.");
    *slash=0;
    if(wcslen(root)>CAP/4)return fail(L"Move Bench to a shorter folder path.");
    swprintf(node,CAP,L"%ls\\bin\\node.exe",root);
    swprintf(script,CAP,L"%ls\\workspace-ui\\desktop-host.mjs",root);
    swprintf(cert,CAP,L"%ls\\certs\\ca-bundle.crt",root);
    if(GetFileAttributesW(node)==INVALID_FILE_ATTRIBUTES||GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES)
        return fail(L"Bench runtime files are missing. Extract the complete downloaded ZIP before opening Bench.exe.");
    HANDLE mutex=CreateMutexW(NULL,TRUE,L"Local\\BenchDesktopHost");
    if(!mutex)return fail(L"Cannot initialize the desktop host.");
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return fail(L"Bench is already running. Use its existing window, or choose Quit there before reopening.");}
    DWORD needed=GetEnvironmentVariableW(L"PATH",NULL,0);
    size_t capacity=wcslen(root)+needed+16;
    wchar_t *search=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,capacity*sizeof(wchar_t));
    if(!search){CloseHandle(mutex);return fail(L"Not enough memory to start Bench.");}
    swprintf(search,capacity,L"%ls\\bin;",root);
    if(needed)GetEnvironmentVariableW(L"PATH",search+wcslen(search),needed);
    BOOL configured=SetEnvironmentVariableW(L"PATH",search)&&SetEnvironmentVariableW(L"OPENAI_CA_BUNDLE",cert);
    HeapFree(GetProcessHeap(),0,search);
    if(!configured){CloseHandle(mutex);return fail(L"Cannot configure the bundled runtime.");}
    swprintf(command,CAP,L"\"%ls\" \"%ls\" --open",node,script);
    STARTUPINFOW startup={0}; PROCESS_INFORMATION process={0};
    startup.cb=sizeof(startup);
    if(!CreateProcessW(node,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,root,&startup,&process)){
        CloseHandle(mutex);return fail(L"Could not start the bundled runtime. Extract the full ZIP to a writable folder and try again.");
    }
    CloseHandle(process.hThread);
    DWORD wait=WaitForSingleObject(process.hProcess,INFINITE),code=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);ReleaseMutex(mutex);CloseHandle(mutex);
    if(code)return fail(L"Bench could not run its local host. Check that Microsoft Edge is installed and port 47291 is free. Launch-workspace.cmd shows diagnostic output.");
    return 0;
}
