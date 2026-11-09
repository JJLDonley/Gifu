#include "engine/process.h"

#include "base/os.h"
#include "base/vec.h"

#include <stdlib.h>
#include <string.h>

// `launcher... program args...` -> a plain spec (argv storage in `storage`).
static ProcessSpec apply_launcher(const ProcessSpec *spec, StringView *storage, isize capacity) {
    if (spec->launcher_count == 0) return *spec;
    isize n = 0;
    for (isize i = 1; i < spec->launcher_count && n < capacity; ++i) storage[n++] = spec->launcher[i];
    if (n < capacity) storage[n++] = spec->program;
    for (isize i = 0; i < spec->arg_count && n < capacity; ++i) storage[n++] = spec->args[i];
    ProcessSpec out = *spec;
    out.program = spec->launcher[0];
    out.args = storage;
    out.arg_count = n;
    out.launcher = NULL;
    out.launcher_count = 0;
    return out;
}

#if defined(OS_WINDOWS)
// =============================================================================
// Windows
// =============================================================================
#include <windows.h>

struct Process {
    PROCESS_INFORMATION info;
    HANDLE stdin_write;
    HANDLE stdout_read;
    HANDLE stderr_read;
};

// Quotes one argument per the MSVCRT / CommandLineToArgvW rules.
static void append_quoted(String *cmd, StringView arg) {
    bool needs_quotes = arg.len == 0;
    for (isize i = 0; i < arg.len && !needs_quotes; ++i) {
        char c = arg.data[i];
        needs_quotes = c == ' ' || c == '\t' || c == '"';
    }
    if (!needs_quotes) {
        string_push(cmd, arg);
        return;
    }
    string_push_char(cmd, '"');
    for (isize i = 0; i < arg.len; ++i) {
        isize backslashes = 0;
        while (i < arg.len && arg.data[i] == '\\') ++i, ++backslashes;
        if (i == arg.len) {
            for (isize k = 0; k < backslashes * 2; ++k) string_push_char(cmd, '\\');
            break;
        }
        if (arg.data[i] == '"') {
            for (isize k = 0; k < backslashes * 2 + 1; ++k) string_push_char(cmd, '\\');
        } else {
            for (isize k = 0; k < backslashes; ++k) string_push_char(cmd, '\\');
        }
        string_push_char(cmd, arg.data[i]);
    }
    string_push_char(cmd, '"');
}

// Every engine runs in one job object that dies with CataGui: a crash or a killed
// GUI must not leave KataGo processes holding the GPU (KataGo's analysis mode
// keeps working through its queued queries after stdin closes).
static HANDLE g_job;
static INIT_ONCE g_job_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK create_job(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once, (void)param, (void)context;
    g_job = CreateJobObjectW(NULL, NULL);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { 0 };
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    return TRUE;
}

Process *process_spawn(const ProcessSpec *original, String *error) {
    InitOnceExecuteOnce(&g_job_once, create_job, NULL, NULL);
    StringView storage[64];
    ProcessSpec flat = apply_launcher(original, storage, countof(storage));
    const ProcessSpec *spec = &flat;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL, err_r = NULL, err_w = NULL;
    if (!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
        if (error) *error = string_format("CreatePipe failed (%lu)", GetLastError());
        return NULL;
    }
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    String cmd = { 0 };
    append_quoted(&cmd, spec->program);
    for (isize i = 0; i < spec->arg_count; ++i) {
        string_push_char(&cmd, ' ');
        append_quoted(&cmd, spec->args[i]);
    }
    wchar_t *wcmd = os_utf8_to_wide(string_sv(&cmd));
    wchar_t *wdir = spec->working_dir.len ? os_utf8_to_wide(spec->working_dir) : NULL;
    string_free(&cmd);

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = err_w;

    Process *proc = calloc(1, sizeof(Process));
    if (!proc) abort();
    // Suspended until it is in the job, so its own children (e.g. ssh) are covered too.
    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, wdir, &si, &proc->info);
    DWORD last_error = GetLastError();
    if (ok) {
        if (g_job) AssignProcessToJobObject(g_job, proc->info.hProcess);
        ResumeThread(proc->info.hThread);
    }
    free(wcmd);
    free(wdir);
    CloseHandle(in_r);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (!ok) {
        if (error) *error = string_format("cannot start " SV_FMT " (error %lu)", SV_ARG(spec->program), last_error);
        CloseHandle(in_w);
        CloseHandle(out_r);
        CloseHandle(err_r);
        free(proc);
        return NULL;
    }
    proc->stdin_write = in_w;
    proc->stdout_read = out_r;
    proc->stderr_read = err_r;
    return proc;
}

static isize read_handle(HANDLE h, char *buffer, isize capacity) {
    DWORD n = 0;
    if (!ReadFile(h, buffer, (DWORD)capacity, &n, NULL)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return (isize)n;
}

isize process_read_stdout(Process *proc, char *buffer, isize capacity) { return read_handle(proc->stdout_read, buffer, capacity); }
isize process_read_stderr(Process *proc, char *buffer, isize capacity) { return read_handle(proc->stderr_read, buffer, capacity); }

bool process_write(Process *proc, StringView data) {
    if (!proc->stdin_write) return false;
    while (data.len > 0) {
        DWORD written = 0;
        if (!WriteFile(proc->stdin_write, data.data, (DWORD)data.len, &written, NULL)) return false;
        data = sv_drop(data, (isize)written);
    }
    return true;
}

void process_close_stdin(Process *proc) {
    if (proc->stdin_write) CloseHandle(proc->stdin_write);
    proc->stdin_write = NULL;
}

void process_kill(Process *proc) { TerminateProcess(proc->info.hProcess, 1); }

bool process_wait(Process *proc, i32 timeout_ms, i32 *exit_code) {
    DWORD rc = WaitForSingleObject(proc->info.hProcess, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    if (rc != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    GetExitCodeProcess(proc->info.hProcess, &code);
    if (exit_code) *exit_code = (i32)code;
    return true;
}

void process_destroy(Process *proc) {
    if (!proc) return;
    process_close_stdin(proc);
    if (!process_wait(proc, 0, NULL)) {
        process_kill(proc);
        process_wait(proc, 2000, NULL);
    }
    CloseHandle(proc->stdout_read);
    CloseHandle(proc->stderr_read);
    CloseHandle(proc->info.hThread);
    CloseHandle(proc->info.hProcess);
    free(proc);
}

#else
// =============================================================================
// POSIX
// =============================================================================
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

struct Process {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    bool exited;
    i32 exit_code;
};

static char *dup_cstr(StringView s) {
    char *p = malloc((usize)s.len + 1);
    if (!p) abort();
    memcpy(p, s.data, (usize)s.len);
    p[s.len] = '\0';
    return p;
}

Process *process_spawn(const ProcessSpec *original, String *error) {
    StringView storage[64];
    ProcessSpec flat = apply_launcher(original, storage, countof(storage));
    const ProcessSpec *spec = &flat;
    signal(SIGPIPE, SIG_IGN);   // writes to a dead engine must not kill the GUI
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) {
        if (error) *error = string_format("pipe() failed: %s", strerror(errno));
        return NULL;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
    int fds[] = { in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1] };
    for (i32 i = 0; i < (i32)countof(fds); ++i) posix_spawn_file_actions_addclose(&actions, fds[i]);
    char *dir = NULL;
    if (spec->working_dir.len) {
        dir = dup_cstr(spec->working_dir);
        posix_spawn_file_actions_addchdir_np(&actions, dir);
    }

    char **argv = calloc((usize)spec->arg_count + 2, sizeof(char *));
    if (!argv) abort();
    argv[0] = dup_cstr(spec->program);
    for (isize i = 0; i < spec->arg_count; ++i) argv[i + 1] = dup_cstr(spec->args[i]);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);   // PATH lookup (ssh, env, ...)
    posix_spawn_file_actions_destroy(&actions);
    for (isize i = 0; i <= spec->arg_count; ++i) free(argv[i]);
    free(argv);
    free(dir);
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (rc != 0) {
        if (error) *error = string_format("cannot start " SV_FMT ": %s", SV_ARG(spec->program), strerror(rc));
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(err_pipe[0]);
        return NULL;
    }
    Process *proc = calloc(1, sizeof(Process));
    if (!proc) abort();
    proc->pid = pid;
    proc->stdin_fd = in_pipe[1];
    proc->stdout_fd = out_pipe[0];
    proc->stderr_fd = err_pipe[0];
    return proc;
}

static isize read_fd(int fd, char *buffer, isize capacity) {
    for (;;) {
        ssize_t n = read(fd, buffer, (size_t)capacity);
        if (n < 0 && errno == EINTR) continue;
        return (isize)n;
    }
}

isize process_read_stdout(Process *proc, char *buffer, isize capacity) { return read_fd(proc->stdout_fd, buffer, capacity); }
isize process_read_stderr(Process *proc, char *buffer, isize capacity) { return read_fd(proc->stderr_fd, buffer, capacity); }

bool process_write(Process *proc, StringView data) {
    if (proc->stdin_fd < 0) return false;
    while (data.len > 0) {
        ssize_t n = write(proc->stdin_fd, data.data, (size_t)data.len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data = sv_drop(data, (isize)n);
    }
    return true;
}

void process_close_stdin(Process *proc) {
    if (proc->stdin_fd >= 0) close(proc->stdin_fd);
    proc->stdin_fd = -1;
}

void process_kill(Process *proc) {
    if (!proc->exited) kill(proc->pid, SIGKILL);
}

bool process_wait(Process *proc, i32 timeout_ms, i32 *exit_code) {
    f64 deadline = os_time_seconds() + timeout_ms / 1000.0;
    while (!proc->exited) {
        int status = 0;
        pid_t rc = waitpid(proc->pid, &status, timeout_ms < 0 ? 0 : WNOHANG);
        if (rc == proc->pid) {
            proc->exited = true;
            proc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        if (rc < 0 && errno != EINTR) {
            proc->exited = true;
            proc->exit_code = -1;
            break;
        }
        if (timeout_ms >= 0 && os_time_seconds() >= deadline) return false;
        if (timeout_ms >= 0) os_sleep_ms(5);
    }
    if (exit_code) *exit_code = proc->exit_code;
    return true;
}

void process_destroy(Process *proc) {
    if (!proc) return;
    process_close_stdin(proc);
    if (!process_wait(proc, 0, NULL)) {
        process_kill(proc);
        process_wait(proc, 2000, NULL);
    }
    close(proc->stdout_fd);
    close(proc->stderr_fd);
    free(proc);
}

#endif
