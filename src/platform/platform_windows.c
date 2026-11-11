// Windows implementation of the platform layer (platform.h): Win32, UTF-16 paths,
// job objects for engines, comdlg32 dialogs, GDI screen capture.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/platform_internal.h"

#include "base/log.h"
#include "base/vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>

// ---- Files, time, paths ---------------------------------------------------------------------------

// =============================================================================
// Windows
// =============================================================================

wchar_t *os_utf8_to_wide(StringView s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data, (int)s.len, NULL, 0);
    wchar_t *w = malloc(sizeof(wchar_t) * (usize)(n + 1));
    if (!w) abort();
    MultiByteToWideChar(CP_UTF8, 0, s.data, (int)s.len, w, n);
    w[n] = 0;
    return w;
}

String os_wide_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    String out = { 0 };
    if (n <= 1) return string_from(SV(""));
    string_reserve(&out, n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data, n, NULL, NULL);
    out.len = n - 1;
    return out;
}

bool os_read_file(StringView path, String *out) {
    wchar_t *wpath = os_utf8_to_wide(path);
    FILE *f = _wfopen(wpath, L"rb");
    free(wpath);
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
    wchar_t *wpath = os_utf8_to_wide(path);
    FILE *f = _wfopen(wpath, L"wb");
    free(wpath);
    if (!f) return false;
    bool ok = fwrite(data.data, 1, (usize)data.len, f) == (usize)data.len;
    fclose(f);
    return ok;
}

bool os_file_exists(StringView path) {
    wchar_t *wpath = os_utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

i64 os_file_mtime(StringView path) {
    wchar_t *wpath = os_utf8_to_wide(path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL ok = GetFileAttributesExW(wpath, GetFileExInfoStandard, &data);
    free(wpath);
    if (!ok) return 0;
    return ((i64)data.ftLastWriteTime.dwHighDateTime << 32) | (i64)data.ftLastWriteTime.dwLowDateTime;
}

i64 os_file_size(StringView path) {
    wchar_t *wpath = os_utf8_to_wide(path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL ok = GetFileAttributesExW(wpath, GetFileExInfoStandard, &data);
    free(wpath);
    return ok ? ((i64)data.nFileSizeHigh << 32) | (i64)data.nFileSizeLow : -1;
}

bool os_rename(StringView from, StringView to) {
    wchar_t *a = os_utf8_to_wide(from), *b = os_utf8_to_wide(to);
    BOOL ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    free(a);
    free(b);
    return ok;
}

bool os_remove(StringView path) {
    wchar_t *w = os_utf8_to_wide(path);
    BOOL ok = DeleteFileW(w);
    free(w);
    return ok;
}

bool os_remove_dir(StringView path) {
    wchar_t *w = os_utf8_to_wide(path);
    BOOL ok = RemoveDirectoryW(w);
    free(w);
    return ok;
}

bool os_set_executable(StringView path) {
    UNUSED(path);
    return true;
}

bool os_dir_exists(StringView path) {
    wchar_t *wpath = os_utf8_to_wide(path);
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool os_make_dirs(StringView path) {
    if (os_dir_exists(path)) return true;
    StringView parent = path_dirname(path);
    if (parent.len > 0 && parent.len < path.len && !sv_eq(parent, path)) os_make_dirs(parent);
    wchar_t *wpath = os_utf8_to_wide(path);
    BOOL ok = CreateDirectoryW(wpath, NULL);
    free(wpath);
    return ok || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool os_list_dir(StringView dir, StringVec *out_names) {
    String pattern = path_join(dir, SV("*"));
    wchar_t *wpattern = os_utf8_to_wide(string_sv(&pattern));
    string_free(&pattern);
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW(wpattern, &data);
    free(wpattern);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        vec_push(out_names, os_wide_to_utf8(data.cFileName));
    } while (FindNextFileW(h, &data));
    FindClose(h);
    if (out_names->len > 1) qsort(out_names->items, (usize)out_names->len, sizeof(String), platform_compare_strings);
    return true;
}

f64 os_time_seconds(void) {
    static LARGE_INTEGER frequency;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (f64)now.QuadPart / (f64)frequency.QuadPart;
}

void os_sleep_ms(i32 ms) { Sleep((DWORD)ms); }

String os_executable_dir(void) {
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, buffer, (DWORD)countof(buffer));
    buffer[n] = 0;
    String full = os_wide_to_utf8(buffer);
    String dir = string_from(path_dirname(string_sv(&full)));
    string_free(&full);
    return dir;
}

String os_config_dir(void) {
    PWSTR appdata = NULL;
    String out = { 0 };
    if (SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata) == S_OK) {
        String base = os_wide_to_utf8(appdata);
        out = path_join(string_sv(&base), SV("Gifu"));
        string_free(&base);
    } else {
        out = string_from(SV("."));
    }
    CoTaskMemFree(appdata);
    return out;
}

const char *os_platform_name(void) { return "windows"; }
const char *os_executable_suffix(void) { return ".exe"; }

// ---- Diagnostics, URLs, detached processes ---------------------------------------------------------

void os_log_backtrace(void) {
    void *frames[32];
    u16 n = CaptureStackBackTrace(1, 32, frames, NULL);
    uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    for (u16 i = 0; i < n; ++i) log_warn("  #%u gifu.exe+0x%llx", (unsigned)i, (unsigned long long)((uintptr_t)frames[i] - base));
}


void os_open_url(StringView url) {
    wchar_t *w = os_utf8_to_wide(url);
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
}

i64 os_process_id(void) { return (i64)GetCurrentProcessId(); }

bool os_wait_process_exit(i64 pid, i32 timeout_ms) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return true;   // already gone
    DWORD rc = WaitForSingleObject(h, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    CloseHandle(h);
    return rc == WAIT_OBJECT_0;
}

bool os_spawn_detached(StringView program, const StringView *args, isize count) {
    String cmd = string_format("\"" SV_FMT "\"", SV_ARG(program));
    for (isize i = 0; i < count; ++i) string_pushf(&cmd, " \"" SV_FMT "\"", SV_ARG(args[i]));
    wchar_t *w = os_utf8_to_wide(string_sv(&cmd));
    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(NULL, w, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
    if (!ok) ok = CreateProcessW(NULL, w, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi);   // breakaway not allowed
    if (ok) CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    free(w);
    string_free(&cmd);
    return ok;
}

// ---- Threads --------------------------------------------------------------------------------------



_Static_assert(sizeof(SRWLOCK) <= sizeof(((Mutex *)0)->storage), "grow Mutex storage");

typedef struct ThreadStart {
    ThreadMain main;
    void *arg;
} ThreadStart;

static DWORD WINAPI thread_trampoline(LPVOID param) {
    ThreadStart start = *(ThreadStart *)param;
    free(param);
    return (DWORD)start.main(start.arg);
}

bool thread_start(Thread *thread, ThreadMain main, void *arg) {
    ThreadStart *start = malloc(sizeof(ThreadStart));
    if (!start) return false;
    *start = (ThreadStart){ main, arg };
    HANDLE h = CreateThread(NULL, 0, thread_trampoline, start, 0, NULL);
    if (!h) {
        free(start);
        return false;
    }
    thread->handle = h;
    return true;
}

void thread_join(Thread *thread) {
    if (!thread->handle) return;
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    thread->handle = NULL;
}
void thread_detach(Thread *thread) {
    if (!thread->handle) return;
    CloseHandle(thread->handle);   // the thread keeps running and cleans up after itself
    thread->handle = NULL;
}

void mutex_init(Mutex *m) { InitializeSRWLock((SRWLOCK *)m->storage); }
void mutex_lock(Mutex *m) { AcquireSRWLockExclusive((SRWLOCK *)m->storage); }
void mutex_unlock(Mutex *m) { ReleaseSRWLockExclusive((SRWLOCK *)m->storage); }
void mutex_destroy(Mutex *m) { UNUSED(m); }

void run_once(OnceFlag *flag, void (*fn)(void)) {
    if (flag->state == 2) return;
    if (InterlockedCompareExchange((volatile LONG *)&flag->state, 1, 0) == 0) {
        fn();
        InterlockedExchange((volatile LONG *)&flag->state, 2);
        return;
    }
    while (flag->state != 2) Sleep(0);
}


// ---- Processes ------------------------------------------------------------------------------------

// =============================================================================
// Windows
// =============================================================================


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

// Every engine runs in one job object that dies with Gifu: a crash or a killed
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
    ProcessSpec flat = platform_apply_launcher(original, storage, countof(storage));
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


// ---- File dialogs ---------------------------------------------------------------------------------




static bool run_dialog(bool save, StringView initial, String *out_path) {
    wchar_t buffer[4096] = { 0 };
    if (save && initial.len) {
        wchar_t *w = os_utf8_to_wide(initial);
        wcsncpy(buffer, w, countof(buffer) - 1);
        free(w);
    }
    wchar_t *dir = !save && initial.len ? os_utf8_to_wide(initial) : NULL;
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"SGF files (*.sgf)\0*.sgf\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (DWORD)countof(buffer);
    ofn.lpstrInitialDir = dir;
    ofn.lpstrDefExt = L"sgf";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    free(dir);
    if (!ok) return false;
    *out_path = os_wide_to_utf8(buffer);
    return true;
}

bool file_dialog_available(void) { return true; }
bool file_dialog_open_sgf(StringView start_dir, String *out_path) { return run_dialog(false, start_dir, out_path); }
bool file_dialog_save_sgf(StringView suggested_path, String *out_path) { return run_dialog(true, suggested_path, out_path); }


// ---- Text encoding --------------------------------------------------------------------------------



static UINT code_page(Charset cs) {
    switch (cs) {
        case CS_GB18030: return 54936;
        case CS_BIG5: return 950;
        case CS_SHIFT_JIS: return 932;
        case CS_EUC_JP: return 20932;
        case CS_EUC_KR: return 949;
        case CS_LATIN1: return 1252;
        default: return 0;
    }
}

bool text_to_utf8(StringView charset, StringView input, String *out) {
    Charset cs = charset_from_label(charset);
    if (cs == CS_UTF8) {
        *out = string_from(input);
        return utf8_is_valid(input);
    }
    UINT cp = code_page(cs);
    if (!cp) return false;
    if (input.len == 0) {
        *out = string_from(SV(""));
        return true;
    }
    int wide_len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, input.data, (int)input.len, NULL, 0);
    if (wide_len <= 0) wide_len = MultiByteToWideChar(cp, 0, input.data, (int)input.len, NULL, 0);   // lenient
    if (wide_len <= 0) return false;
    wchar_t *wide = malloc(sizeof(wchar_t) * (usize)wide_len);
    if (!wide) return false;
    MultiByteToWideChar(cp, 0, input.data, (int)input.len, wide, wide_len);
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL);
    String result = { 0 };
    string_reserve(&result, len);
    WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, result.data, len, NULL, NULL);
    result.len = len;
    result.data[len] = '\0';
    free(wide);
    *out = result;
    return true;
}


// ---- Screen capture (GDI) ---------------------------------------------------------------------------

static bool capture_grab_gdi(const ScreenRect *rect, RgbaImage *out, String *error) {
    i32 vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    i32 vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    ScreenRect r = rect ? *rect : (ScreenRect){ vx, vy, vw, vh };   // desktop coordinates (may be negative)
    i32 x0 = MAX(r.x, vx), y0 = MAX(r.y, vy), x1 = MIN(r.x + r.width, vx + vw), y1 = MIN(r.y + r.height, vy + vh);
    if (x1 <= x0 || y1 <= y0 || !rgba_image_alloc(out, x1 - x0, y1 - y0)) {
        capture_set_error(error, string_from(SV("the area is outside the screen")));
        return false;
    }
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = { .bmiHeader = { sizeof(BITMAPINFOHEADER), out->width, -out->height, 1, 32, BI_RGB } };
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    bool ok = bmp && bits;
    if (ok) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = BitBlt(mem, 0, 0, out->width, out->height, screen, x0, y0, SRCCOPY | CAPTUREBLT) != 0;
        GdiFlush();
        const u8 *src = bits;
        for (isize i = 0; ok && i < (isize)out->width * out->height; ++i) {   // BGRX -> RGBA
            out->pixels[i * 4 + 0] = src[i * 4 + 2];
            out->pixels[i * 4 + 1] = src[i * 4 + 1];
            out->pixels[i * 4 + 2] = src[i * 4 + 0];
            out->pixels[i * 4 + 3] = 255;
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    if (!ok) {
        rgba_image_free(out);
        capture_set_error(error, string_from(SV("BitBlt failed")));
    }
    return ok;
}

// ---- Screen capture: this system's backends -------------------------------------------------------

CaptureKind capture_default_kind(StringView command) { return command.len ? CAPTURE_COMMAND : CAPTURE_GDI; }
bool capture_kind_available(CaptureKind kind) { return kind == CAPTURE_GDI; }
bool capture_grab_native(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    UNUSED(c);
    return capture_grab_gdi(rect, out, error);
}
void capture_close_native(ScreenCapture *c) { UNUSED(c); }
