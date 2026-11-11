#include "base/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(OS_WINDOWS)
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <dirent.h>
    #include <errno.h>
    #include <sys/stat.h>
    #include <time.h>
    #include <unistd.h>
#endif

void string_vec_free(StringVec *v) {
    vec_foreach(String, s, v) string_free(s);
    vec_free(v);
}

static int compare_strings(const void *a, const void *b) {
    const String *x = a, *y = b;
    return strcmp(string_cstr(x), string_cstr(y));
}

// ---- Paths (shared) ------------------------------------------------------------

static bool is_separator(char c) { return c == '/' || c == '\\'; }

String path_join(StringView a, StringView b) {
    String out = string_from(a);
    if (out.len > 0 && !is_separator(out.data[out.len - 1])) string_push_char(&out, '/');
    string_push(&out, b);
    return out;
}

StringView path_basename(StringView path) {
    for (isize i = path.len - 1; i >= 0; --i) {
        if (is_separator(path.data[i])) return sv_drop(path, i + 1);
    }
    return path;
}

StringView path_dirname(StringView path) {
    for (isize i = path.len - 1; i >= 0; --i) {
        if (is_separator(path.data[i])) return sv_take(path, i);
    }
    return SV(".");
}

StringView path_extension(StringView path) {
    StringView base = path_basename(path);
    isize dot = sv_find_last_char(base, '.');
    return dot <= 0 ? SV_NULL : sv_drop(base, dot);
}

#if defined(OS_WINDOWS)
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
    if (out_names->len > 1) qsort(out_names->items, (usize)out_names->len, sizeof(String), compare_strings);
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

#else
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
    return (i64)st.st_mtim.tv_sec * 1000000000LL + (i64)st.st_mtim.tv_nsec;
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
    if (out_names->len > 1) qsort(out_names->items, (usize)out_names->len, sizeof(String), compare_strings);
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

String os_executable_dir(void) {
    char buffer[4096];
    isize n = (isize)readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) return string_from(SV("."));
    buffer[n] = '\0';
    return string_from(path_dirname(sv_from_parts(buffer, n)));
}

String os_config_dir(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return path_join(sv(xdg), SV("gifu"));
    const char *home = getenv("HOME");
    String base = path_join(sv(home ? home : "."), SV(".config"));
    String out = path_join(string_sv(&base), SV("gifu"));
    string_free(&base);
    return out;
}

const char *os_platform_name(void) {
#if defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

#endif

// ---- Backtraces (diagnostics) -------------------------------------------------------------------------

#include "base/log.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
void os_log_backtrace(void) {
    void *frames[32];
    u16 n = CaptureStackBackTrace(1, 32, frames, NULL);
    uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
    for (u16 i = 0; i < n; ++i) log_warn("  #%u gifu.exe+0x%llx", (unsigned)i, (unsigned long long)((uintptr_t)frames[i] - base));
}
#elif defined(__linux__)
#include <execinfo.h>
void os_log_backtrace(void) {
    void *frames[32];
    int n = backtrace(frames, 32);
    char **names = backtrace_symbols(frames, n);
    for (int i = 1; i < n; ++i) log_warn("  #%d %s", i - 1, names ? names[i] : "?");
    free(names);
}
#else
void os_log_backtrace(void) {}
#endif
