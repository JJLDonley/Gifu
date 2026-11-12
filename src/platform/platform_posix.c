// POSIX implementation shared by Linux and macOS (platform.h): files, time,
// pthreads, posix_spawn processes, iconv, backtraces, detached processes.
#include "platform/platform_internal.h"

#include "base/log.h"
#include "base/vec.h"

#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const char *os_executable_suffix(void) { return ""; }

// ---- Files, time ----------------------------------------------------------------------------------

// =============================================================================
// POSIX
// =============================================================================

static char *to_cstr(StringView s) {
    char *p = malloc((usize)s.len + 1);
    if (!p) abort();
    memcpy(p, s.data, (usize)s.len);
    p[s.len] = '\0';
    return p;
}

bool os_read_file(StringView path, String *out) {
    char *cpath = to_cstr(path);
    FILE *f = fopen(cpath, "rb");
    free(cpath);
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    string_clear(out);
    string_reserve(out, size);
    out->len = (isize)fread(out->data, 1, (usize)size, f);
    out->data[out->len] = '\0';
    fclose(f);
    return true;
}

bool os_write_file(StringView path, StringView data) {
    char *cpath = to_cstr(path);
    FILE *f = fopen(cpath, "wb");
    free(cpath);
    if (!f) return false;
    bool ok = fwrite(data.data, 1, (usize)data.len, f) == (usize)data.len;
    fclose(f);
    return ok;
}

static bool stat_mode(StringView path, mode_t *mode) {
    char *cpath = to_cstr(path);
    struct stat st;
    int rc = stat(cpath, &st);
    free(cpath);
    if (rc != 0) return false;
    *mode = st.st_mode;
    return true;
}

bool os_file_exists(StringView path) {
    mode_t mode;
    return stat_mode(path, &mode) && S_ISREG(mode);
}

i64 os_file_mtime(StringView path) {
    char *cpath = to_cstr(path);
    struct stat st;
    int rc = stat(cpath, &st);
    free(cpath);
    if (rc != 0) return 0;
    return platform_stat_mtime_ns(&st);
}

i64 os_file_size(StringView path) {
    char *cpath = to_cstr(path);
    struct stat st;
    int rc = stat(cpath, &st);
    free(cpath);
    return rc == 0 ? (i64)st.st_size : -1;
}

bool os_rename(StringView from, StringView to) {
    char *a = to_cstr(from), *b = to_cstr(to);
    int rc = rename(a, b);
    free(a);
    free(b);
    return rc == 0;
}

bool os_remove(StringView path) {
    char *c = to_cstr(path);
    int rc = remove(c);
    free(c);
    return rc == 0;
}

bool os_remove_dir(StringView path) {
    char *c = to_cstr(path);
    int rc = rmdir(c);
    free(c);
    return rc == 0;
}

bool os_set_executable(StringView path) {
    char *c = to_cstr(path);
    int rc = chmod(c, 0755);
    free(c);
    return rc == 0;
}

bool os_dir_exists(StringView path) {
    mode_t mode;
    return stat_mode(path, &mode) && S_ISDIR(mode);
}

bool os_make_dirs(StringView path) {
    if (path.len == 0 || os_dir_exists(path)) return true;
    StringView parent = path_dirname(path);
    if (parent.len > 0 && parent.len < path.len) os_make_dirs(parent);
    char *cpath = to_cstr(path);
    int rc = mkdir(cpath, 0755);
    free(cpath);
    return rc == 0 || errno == EEXIST;
}

bool os_list_dir(StringView dir, StringVec *out_names) {
    char *cdir = to_cstr(dir);
    DIR *d = opendir(cdir);
    free(cdir);
    if (!d) return false;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        vec_push(out_names, string_from(sv(entry->d_name)));
    }
    closedir(d);
    if (out_names->len > 1) qsort(out_names->items, (usize)out_names->len, sizeof(String), platform_compare_strings);
    return true;
}

f64 os_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1e-9;
}

void os_sleep_ms(i32 ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

// ---- Diagnostics, detached processes ---------------------------------------------------------------


void os_log_backtrace(void) {
    void *frames[32];
    int n = backtrace(frames, 32);
    char **names = backtrace_symbols(frames, n);
    for (int i = 1; i < n; ++i) log_warn("  #%d %s", i - 1, names ? names[i] : "?");
    free(names);
}

#include <signal.h>
#include <spawn.h>
extern char **environ;

i64 os_process_id(void) { return (i64)getpid(); }


bool os_wait_process_exit(i64 pid, i32 timeout_ms) {
    for (i32 waited = 0; timeout_ms < 0 || waited <= timeout_ms; waited += 50) {
        if (kill((pid_t)pid, 0) != 0) return true;   // no such process any more
        os_sleep_ms(50);
    }
    return false;
}
#include <fcntl.h>

// Children must not inherit Gifu's other descriptors: an engine whose stdin pipe is
// also held by another child (or by the next Gifu after an update) never sees EOF,
// so it outlives a Gifu that crashed or was killed.
static void spawn_close_others(posix_spawn_file_actions_t *actions, short *flags) {
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    *flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;   // macOS: only what the actions set up survives the exec
    (void)actions;
#elif defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
    posix_spawn_file_actions_addclosefrom_np(actions, STDERR_FILENO + 1);   // last: after the dup2s and closes
#endif
    (void)flags;
#else
    (void)actions, (void)flags;   // the pipes are close-on-exec at least
#endif
}

// A pipe both of whose ends are close-on-exec (the child's end is dup2'd, which clears it).
static int pipe_cloexec(int fds[2]) {
#ifdef __linux__
    return pipe2(fds, O_CLOEXEC);
#else
    if (pipe(fds) != 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}

bool os_spawn_detached(StringView program, const StringView *args, isize count) {
    char **argv = calloc((usize)count + 2, sizeof(char *));
    if (!argv) abort();
    String prog = string_from(program);
    argv[0] = (char *)string_cstr(&prog);
    String *copies = calloc((usize)count + 1, sizeof(String));
    if (!copies) abort();
    for (isize i = 0; i < count; ++i) copies[i] = string_from(args[i]), argv[i + 1] = (char *)string_cstr(&copies[i]);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    short flags = 0;
#ifdef POSIX_SPAWN_SETSID
    flags |= POSIX_SPAWN_SETSID;
#endif
    spawn_close_others(&actions, &flags);   // the new Gifu after an update must not hold the old one's engine pipes
    posix_spawnattr_setflags(&attr, flags);
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &actions, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    for (isize i = 0; i < count; ++i) string_free(&copies[i]);
    free(copies);
    string_free(&prog);
    free(argv);
    return rc == 0;
}

// ---- Threads --------------------------------------------------------------------------------------

#include <pthread.h>

_Static_assert(sizeof(pthread_mutex_t) <= sizeof(((Mutex *)0)->storage), "grow Mutex storage");

typedef struct ThreadStart {
    ThreadMain main;
    void *arg;
} ThreadStart;

static void *thread_trampoline(void *param) {
    ThreadStart start = *(ThreadStart *)param;
    free(param);
    start.main(start.arg);
    return NULL;
}

bool thread_start(Thread *thread, ThreadMain main, void *arg) {
    ThreadStart *start = malloc(sizeof(ThreadStart));
    if (!start) return false;
    *start = (ThreadStart){ main, arg };
    pthread_t *t = malloc(sizeof(pthread_t));
    if (!t || pthread_create(t, NULL, thread_trampoline, start) != 0) {
        free(start);
        free(t);
        return false;
    }
    thread->handle = t;
    return true;
}

void thread_join(Thread *thread) {
    if (!thread->handle) return;
    pthread_join(*(pthread_t *)thread->handle, NULL);
    free(thread->handle);
    thread->handle = NULL;
}
void thread_detach(Thread *thread) {
    if (!thread->handle) return;
    pthread_detach(*(pthread_t *)thread->handle);
    free(thread->handle);
    thread->handle = NULL;
}

void mutex_init(Mutex *m) { pthread_mutex_init((pthread_mutex_t *)m->storage, NULL); }
void mutex_lock(Mutex *m) { pthread_mutex_lock((pthread_mutex_t *)m->storage); }
void mutex_unlock(Mutex *m) { pthread_mutex_unlock((pthread_mutex_t *)m->storage); }
void mutex_destroy(Mutex *m) { pthread_mutex_destroy((pthread_mutex_t *)m->storage); }

static pthread_mutex_t g_once_lock = PTHREAD_MUTEX_INITIALIZER;

void run_once(OnceFlag *flag, void (*fn)(void)) {
    if (__atomic_load_n(&flag->state, __ATOMIC_ACQUIRE) == 2) return;
    pthread_mutex_lock(&g_once_lock);
    if (flag->state != 2) {
        fn();
        __atomic_store_n(&flag->state, 2, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_once_lock);
}

// ---- Processes ------------------------------------------------------------------------------------

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

// ---- Children and termination signals ----
// The children alive now, in slots the signal handler can read (0 = free).
enum { CHILD_SLOTS = 64 };
static pid_t g_children[CHILD_SLOTS];

static void children_add(pid_t pid) {
    for (i32 i = 0; i < CHILD_SLOTS; ++i) {
        pid_t free_slot = 0;
        if (__atomic_compare_exchange_n(&g_children[i], &free_slot, pid, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
    }
    log_warn("process: more than %d children; pid %d is not stopped on a termination signal", CHILD_SLOTS, (int)pid);
}

static void children_remove(pid_t pid) {
    for (i32 i = 0; i < CHILD_SLOTS; ++i) {
        pid_t mine = pid;
        if (__atomic_compare_exchange_n(&g_children[i], &mine, 0, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
    }
}

// Termination signals and crashes: the engines (and every other child) are told to
// stop, then the signal does what it would have done (exit, core dump).
static void on_termination(int sig) {
    for (i32 i = 0; i < CHILD_SLOTS; ++i) {
        pid_t pid = __atomic_load_n(&g_children[i], __ATOMIC_SEQ_CST);
        if (pid > 0) kill(pid, SIGTERM);
    }
    raise(sig);   // SA_RESETHAND restored the default action; it runs when this handler returns
}

static void install_termination_handlers(void) {
    signal(SIGPIPE, SIG_IGN);   // writes to a dead engine must not kill the GUI
    const int signals[] = { SIGTERM, SIGINT, SIGHUP, SIGQUIT, SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    for (i32 i = 0; i < (i32)countof(signals); ++i) {
        struct sigaction old;
        if (sigaction(signals[i], NULL, &old) != 0) continue;
        // Someone else's choice stays: ignored (nohup), a debugger's or a sanitizer's handler.
        if ((old.sa_flags & SA_SIGINFO) || old.sa_handler != SIG_DFL) continue;
        struct sigaction sa = { 0 };
        sa.sa_handler = on_termination;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;
        sigaction(signals[i], &sa, NULL);
    }
}

static pthread_once_t g_handlers_once = PTHREAD_ONCE_INIT;

Process *process_spawn(const ProcessSpec *original, String *error) {
    StringView storage[64];
    ProcessSpec flat = platform_apply_launcher(original, storage, countof(storage));
    const ProcessSpec *spec = &flat;
    pthread_once(&g_handlers_once, install_termination_handlers);
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe_cloexec(in_pipe) || pipe_cloexec(out_pipe) || pipe_cloexec(err_pipe)) {
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
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = 0;
    spawn_close_others(&actions, &flags);
    posix_spawnattr_setflags(&attr, flags);

    char **argv = calloc((usize)spec->arg_count + 2, sizeof(char *));
    if (!argv) abort();
    argv[0] = dup_cstr(spec->program);
    for (isize i = 0; i < spec->arg_count; ++i) argv[i + 1] = dup_cstr(spec->args[i]);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &actions, &attr, argv, environ);   // PATH lookup (ssh, env, ...)
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
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
    children_add(pid);
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
            children_remove(proc->pid);   // reaped: its pid may be reused
            break;
        }
        if (rc < 0 && errno != EINTR) {
            proc->exited = true;
            proc->exit_code = -1;
            children_remove(proc->pid);
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
    children_remove(proc->pid);   // given up on (not reaped): no longer Gifu's to stop
    close(proc->stdout_fd);
    close(proc->stderr_fd);
    free(proc);
}


// ---- Text encoding (iconv) --------------------------------------------------------------------------

#include <errno.h>
#include <iconv.h>

static const char *iconv_name(Charset cs) {
    switch (cs) {
        case CS_GB18030: return "GB18030";
        case CS_BIG5: return "BIG5-HKSCS";
        case CS_SHIFT_JIS: return "CP932";
        case CS_EUC_JP: return "EUC-JP";
        case CS_EUC_KR: return "CP949";
        case CS_LATIN1: return "CP1252";
        default: return NULL;
    }
}

bool text_to_utf8(StringView charset, StringView input, String *out) {
    Charset cs = charset_from_label(charset);
    if (cs == CS_UTF8) {
        *out = string_from(input);
        return utf8_is_valid(input);
    }
    const char *name = iconv_name(cs);
    if (!name) return false;
    iconv_t cd = iconv_open("UTF-8", name);
    if (cd == (iconv_t)-1) return false;
    String result = { 0 };
    string_reserve(&result, input.len * 2 + 16);
    char *in = (char *)input.data;
    size_t in_left = (size_t)input.len;
    bool ok = true;
    while (in_left > 0) {
        char *dst = result.data + result.len;
        size_t out_left = (size_t)(result.cap - 1 - result.len);
        size_t rc = iconv(cd, &in, &in_left, &dst, &out_left);
        result.len = dst - result.data;
        if (rc != (size_t)-1) continue;
        if (errno == E2BIG) {
            string_reserve(&result, result.cap * 2);
        } else if (errno == EILSEQ || errno == EINVAL) {
            // Skip one bad byte, emit U+FFFD, keep going (damaged files are common).
            string_reserve(&result, result.len + 4);
            memcpy(result.data + result.len, "\xEF\xBF\xBD", 3);
            result.len += 3;
            in++;
            in_left--;
        } else {
            ok = false;
            break;
        }
    }
    iconv_close(cd);
    result.data[result.len] = '\0';
    if (!ok) {
        string_free(&result);
        return false;
    }
    *out = result;
    return true;
}
