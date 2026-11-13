#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "runner.h"
#include <gio/gio.h>
#include <string.h>
#include <errno.h>
#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#define LOG_LIMIT (256*1024)
static void append(Runner *r,const char *s,gsize n) {
    gsize start=r->output->len;
    g_string_append_len(r->output,s,n);
    for(gsize i=start;i<r->output->len;i++) if(!r->output->str[i]) r->output->str[i]='?';
    r->output_bytes+=n;
    if(r->output->len>LOG_LIMIT) { g_string_erase(r->output,0,r->output->len-LOG_LIMIT); r->truncated=TRUE; }
}
static char **child_environment(void) {
    char **env=g_get_environ();
    /* Environment names are case-insensitive on Windows. */
    for(guint i=0;env[i];) {
        if(!g_ascii_strncasecmp(env[i],"OPENAI_API_KEY=",15)) {
            char *name=g_strndup(env[i],14); env=g_environ_unsetenv(env,name); g_free(name); i=0;
        } else i++;
    }
    return env;
}
const char *runner_shell(void) {
#ifdef G_OS_WIN32
    return "Windows cmd.exe /D /S /C (use cmd syntax; invoke powershell explicitly if needed)";
#else
    return "/bin/sh -c (POSIX shell)";
#endif
}
#ifdef G_OS_WIN32
static gboolean win_error(GError **e,const char *where) {
    g_set_error(e,G_IO_ERROR,G_IO_ERROR_FAILED,"%s failed (Windows error %lu)",where,GetLastError()); return FALSE;
}
static gint env_compare(gconstpointer a,gconstpointer b) {
    return g_ascii_strcasecmp(*(char *const *)a,*(char *const *)b);
}
static wchar_t *environment_block(void) {
    char **env=child_environment();
    GPtrArray *entries=g_ptr_array_new();
    for(guint i=0;env[i];i++) g_ptr_array_add(entries,env[i]);
    g_ptr_array_sort(entries,env_compare);
    GArray *wide=g_array_new(FALSE,TRUE,sizeof(wchar_t));
    for(guint i=0;i<entries->len;i++) {
        glong n=0; gunichar2 *s=g_utf8_to_utf16(g_ptr_array_index(entries,i),-1,NULL,&n,NULL);
        if(s) { g_array_append_vals(wide,s,n+1); g_free(s); }
    }
    wchar_t zero=0; g_array_append_val(wide,zero);
    g_ptr_array_unref(entries); g_strfreev(env); return (wchar_t *)g_array_free(wide,FALSE);
}
gboolean runner_start(Runner *r,const char *cwd,const char *command,guint seconds,GError **e) {
    if(r->active) { g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_BUSY,"Command already running"); return FALSE; }
    runner_clear(r); r->output=g_string_new(NULL); r->exit_code=-1;
    SECURITY_ATTRIBUTES sa={sizeof sa,NULL,TRUE}; HANDLE write_pipe=NULL,read_pipe=NULL;
    HANDLE input=INVALID_HANDLE_VALUE; PROCESS_INFORMATION pi={0};
    wchar_t shell[MAX_PATH]; UINT count=GetSystemDirectoryW(shell,MAX_PATH);
    if(!count || count+9>=MAX_PATH) return win_error(e,"GetSystemDirectory");
    wcscat(shell,L"\\cmd.exe");
    char *shell_utf8=g_utf16_to_utf8((gunichar2 *)shell,-1,NULL,NULL,NULL);
    char *line=g_strdup_printf("\"%s\" /D /S /C \"%s\"",shell_utf8,command); g_free(shell_utf8);
    wchar_t *wide_line=(wchar_t *)g_utf8_to_utf16(line,-1,NULL,NULL,e); g_free(line);
    if(!wide_line) return FALSE;
    wchar_t *wide_cwd=(wchar_t *)g_utf8_to_utf16(cwd,-1,NULL,NULL,e);
    if(!wide_cwd) { g_free(wide_line); return FALSE; }
    wchar_t *env=environment_block(); gboolean ok=FALSE;
    if(!CreatePipe(&read_pipe,&write_pipe,&sa,0) || !SetHandleInformation(read_pipe,HANDLE_FLAG_INHERIT,0)) goto done;
    input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    if(input==INVALID_HANDLE_VALUE) goto done;
    r->job=CreateJobObjectW(NULL,NULL); if(!r->job) goto done;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(r->job,JobObjectExtendedLimitInformation,&limits,sizeof limits)) goto done;
    STARTUPINFOW si={0}; si.cb=sizeof si; si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdInput=input; si.hStdOutput=write_pipe; si.hStdError=write_pipe;
    if(!CreateProcessW(shell,wide_line,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,
                       env,wide_cwd,&si,&pi)) goto done;
    if(!AssignProcessToJobObject(r->job,pi.hProcess) || ResumeThread(pi.hThread)==(DWORD)-1) goto done;
    r->process=pi.hProcess; r->pipe=read_pipe; read_pipe=NULL;
    r->active=TRUE; r->deadline=g_get_monotonic_time()+(gint64)seconds*G_USEC_PER_SEC; ok=TRUE;
done:
    if(!ok) { win_error(e,"Command launch"); if(pi.hProcess) { TerminateProcess(pi.hProcess,127); CloseHandle(pi.hProcess); } }
    if(pi.hThread) CloseHandle(pi.hThread);
    if(input!=INVALID_HANDLE_VALUE) CloseHandle(input);
    if(write_pipe) CloseHandle(write_pipe);
    if(read_pipe) CloseHandle(read_pipe);
    if(!ok && r->job) { CloseHandle(r->job); r->job=NULL; }
    g_free(env); g_free(wide_line); g_free(wide_cwd); return ok;
}
static void drain(Runner *r) {
    char buffer[8192]; DWORD available,n;
    /* Bounded per tick so a noisy process cannot starve GTK. */
    for(int i=0;i<32 && PeekNamedPipe(r->pipe,NULL,0,NULL,&available,NULL) && available;i++) {
        if(!ReadFile(r->pipe,buffer,MIN(available,sizeof buffer),&n,NULL) || !n) break;
        append(r,buffer,n);
    }
}
void runner_cancel(Runner *r) {
    if(r->active && !r->cancelled) { r->cancelled=TRUE; TerminateJobObject(r->job,125); }
}
gboolean runner_poll(Runner *r) {
    if(!r->active) return TRUE;
    if(!r->cancelled && g_get_monotonic_time()>r->deadline) { r->timed_out=TRUE; runner_cancel(r); }
    drain(r);
    if(WaitForSingleObject(r->process,0)==WAIT_TIMEOUT) return FALSE;
    DWORD code=127; GetExitCodeProcess(r->process,&code); r->exit_code=(int)code;
    /* A command is complete when its shell exits; stop any background children. */
    TerminateJobObject(r->job,125); drain(r);
    CloseHandle(r->process); CloseHandle(r->job); CloseHandle(r->pipe);
    r->process=r->job=r->pipe=NULL; r->active=FALSE; return TRUE;
}
#else
gboolean runner_start(Runner *r,const char *cwd,const char *command,guint seconds,GError **e) {
    if(r->active) { g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_BUSY,"Command already running"); return FALSE; }
    runner_clear(r); r->output=g_string_new(NULL); r->exit_code=-1;
    int fds[2]; if(pipe(fds)<0) goto error;
    if(fcntl(fds[0],F_SETFL,O_NONBLOCK)<0 || fcntl(fds[0],F_SETFD,FD_CLOEXEC)<0) { close(fds[0]); close(fds[1]); goto error; }
    char **env=child_environment(); int pid=fork();
    if(pid==0) {
        if(setpgid(0,0)<0 || chdir(cwd)<0) _exit(127);
        int input=open("/dev/null",O_RDONLY);
        if(input<0 || dup2(input,STDIN_FILENO)<0 || dup2(fds[1],STDOUT_FILENO)<0 || dup2(fds[1],STDERR_FILENO)<0) _exit(127);
        close(input); close(fds[0]); close(fds[1]);
        char *const argv[]={"sh","-c",(char *)command,NULL}; execve("/bin/sh",argv,env); _exit(127);
    }
    g_strfreev(env); close(fds[1]);
    if(pid<0) { close(fds[0]); goto error; }
    setpgid(pid,pid); r->pid=pid; r->pipe_fd=fds[0]; r->active=TRUE;
    r->deadline=g_get_monotonic_time()+(gint64)seconds*G_USEC_PER_SEC; return TRUE;
error:
    g_set_error(e,G_IO_ERROR,g_io_error_from_errno(errno),"Command launch: %s",g_strerror(errno)); return FALSE;
}
static void drain(Runner *r) {
    char buffer[8192];
    for(int i=0;i<32;i++) { ssize_t n=read(r->pipe_fd,buffer,sizeof buffer); if(n<=0) break; append(r,buffer,n); }
}
void runner_cancel(Runner *r) {
    if(r->active && !r->cancelled) { r->cancelled=TRUE; kill(-r->pid,SIGKILL); }
}
gboolean runner_poll(Runner *r) {
    if(!r->active) return TRUE;
    if(!r->cancelled && g_get_monotonic_time()>r->deadline) { r->timed_out=TRUE; runner_cancel(r); }
    drain(r); int status=0; pid_t p=waitpid(r->pid,&status,WNOHANG);
    if(p==0 || (p<0 && errno==EINTR)) return FALSE;
    r->exit_code=p<0?127:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    kill(-r->pid,SIGKILL); drain(r); close(r->pipe_fd); r->pipe_fd=-1; r->pid=0; r->active=FALSE; return TRUE;
}
#endif
void runner_clear(Runner *r) {
    if(r->active) {
        runner_cancel(r);
#ifdef G_OS_WIN32
        WaitForSingleObject(r->process,5000);
        CloseHandle(r->process); CloseHandle(r->job); CloseHandle(r->pipe);
#else
        while(waitpid(r->pid,NULL,0)<0 && errno==EINTR) {}
        close(r->pipe_fd);
#endif
    }
    if(r->output) g_string_free(r->output,TRUE);
    memset(r,0,sizeof *r);
}
