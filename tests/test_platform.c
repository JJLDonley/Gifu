// The behaviour every system's platform layer must share (src/platform/platform.h).
// CI runs these on Linux, Windows and macOS.
#include "test.h"

#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif

static String self_program(void);

// Reads one line from `p`'s stdout (without the newline).
static String read_line(Process *p) {
    String line = { 0 };
    char c;
    while (process_read_stdout(p, &c, 1) == 1 && c != '\n') string_push(&line, sv_from_parts(&c, 1));
    return line;
}

// The test program run as a child process (tests/test_main.c: --platform-child MODE).
int platform_child_main(const char *mode);
int platform_child_main(const char *mode) {
    if (strcmp(mode, "report-sleep") == 0) {   // its pid on stdout, then until killed
        printf("%lld\n", (long long)os_process_id());
        fflush(stdout);
        for (;;) os_sleep_ms(100);
    }
#ifndef _WIN32
    if (strcmp(mode, "terminated") == 0) {   // starts a report-sleep child, passes its pid on, then gets SIGTERM
        String exe = self_program();
        StringView args[] = { SV("--platform-child"), SV("report-sleep") };
        ProcessSpec spec = { string_sv(&exe), args, countof(args), SV(""), NULL, 0 };
        Process *p = process_spawn(&spec, NULL);
        if (!p) return 1;
        String pid = read_line(p);
        printf("%s\n", string_cstr(&pid));
        fflush(stdout);
        raise(SIGTERM);
        return 4;   // not reached: SIGTERM ends the process
    }
#endif
    if (strcmp(mode, "echo") == 0) {   // one line from stdin back to stdout, upper-cased; exit code 3
        char line[256];
        if (!fgets(line, sizeof(line), stdin)) return 1;
        for (char *p = line; *p; ++p) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
        fputs(line, stdout);
        fflush(stdout);
        return 3;
    }
    if (strcmp(mode, "sleep") == 0) {   // until killed
        for (;;) os_sleep_ms(100);
    }
    return 2;
}

static String test_dir(const char *name) {
    String d = path_join(SV(GIFU_TEST_OUTPUT_DIR), sv(name));
    os_remove_tree(string_sv(&d));
    CHECK(os_make_dirs(string_sv(&d)));
    return d;
}

static void test_paths(void) {
    String p = path_join(SV("a dir/棋譜 フォルダ"), SV("game 1.sgf"));
    CHECK_SV(path_basename(string_sv(&p)), SV("game 1.sgf"));
    CHECK_SV(path_dirname(string_sv(&p)), SV("a dir/棋譜 フォルダ"));
    CHECK_SV(path_extension(string_sv(&p)), SV(".sgf"));
    string_free(&p);
    CHECK_SV(path_basename(SV("C:\\Go\\x.sgf")), SV("x.sgf"));   // both separators everywhere
    String exe = os_executable_dir(), cfg = os_config_dir();
    CHECK(exe.len > 0 && os_dir_exists(string_sv(&exe)) && cfg.len > 0);
    string_free(&exe), string_free(&cfg);
    StringView platform = sv(os_platform_name());
    CHECK(sv_eq(platform, SV("windows")) == (strcmp(os_executable_suffix(), ".exe") == 0));
    CHECK(sv_eq(platform, SV("windows")) || sv_eq(platform, SV("linux")) || sv_eq(platform, SV("macos")));
}

static void test_files(void) {
    String dir = test_dir("test-platform-files 棋");
    String a = path_join(string_sv(&dir), SV("ä file.txt")), b = path_join(string_sv(&dir), SV("b.txt"));
    CHECK(os_write_file(string_sv(&a), SV("hello")) && os_write_file(string_sv(&b), SV("old")));
    CHECK(os_file_exists(string_sv(&a)) && os_file_size(string_sv(&a)) == 5 && os_file_mtime(string_sv(&a)) > 0);
    CHECK(os_rename(string_sv(&a), string_sv(&b)));   // replaces an existing file
    String text = { 0 };
    CHECK(os_read_file(string_sv(&b), &text) && sv_eq(string_sv(&text), SV("hello")));
    CHECK(!os_file_exists(string_sv(&a)));
    String deep = path_join(string_sv(&dir), SV("x/y/z"));
    CHECK(os_make_dirs(string_sv(&deep)) && os_dir_exists(string_sv(&deep)));
    String c = path_join(string_sv(&dir), SV("C.txt"));
    CHECK(os_write_file(string_sv(&c), SV("")));
    StringVec names = { 0 };
    CHECK(os_list_dir(string_sv(&dir), &names) && names.len == 3);   // b.txt, C.txt, x
    for (isize i = 1; i < names.len; ++i) CHECK(strcmp(string_cstr(&names.items[i - 1]), string_cstr(&names.items[i])) < 0);   // sorted
    string_vec_free(&names);
    CHECK(os_remove_tree(string_sv(&dir)) && !os_dir_exists(string_sv(&dir)));
    string_free(&a), string_free(&b), string_free(&c), string_free(&deep), string_free(&text), string_free(&dir);
}

static String self_program(void) {
    String dir = os_executable_dir();
    String exe = path_join(string_sv(&dir), SV("gifu_tests"));
    string_push_cstr(&exe, os_executable_suffix());
    string_free(&dir);
    return exe;
}

static void test_processes(void) {
    String exe = self_program();
    StringView args[] = { SV("--platform-child"), SV("echo") };
    ProcessSpec spec = { string_sv(&exe), args, countof(args), SV(""), NULL, 0 };
    String err = { 0 };
    Process *p = process_spawn(&spec, &err);
    CHECK(p != NULL);
    if (p) {
        CHECK(process_write(p, SV("go stones\n")));
        process_close_stdin(p);
        char buffer[64];
        String out = { 0 };
        for (isize n; (n = process_read_stdout(p, buffer, (isize)sizeof(buffer))) > 0;) string_push(&out, sv_from_parts(buffer, n));
        i32 code = -1;
        CHECK(process_wait(p, 10000, &code) && code == 3);
        CHECK_SV(sv_trim(string_sv(&out)), SV("GO STONES"));
        string_free(&out);
        process_destroy(p);
    }
    StringView sleep_args[] = { SV("--platform-child"), SV("sleep") };
    ProcessSpec sleeper = { string_sv(&exe), sleep_args, countof(sleep_args), SV(""), NULL, 0 };
    p = process_spawn(&sleeper, &err);
    CHECK(p != NULL);
    if (p) {
        i32 code = 0;
        CHECK(!process_wait(p, 200, &code));   // still running
        process_kill(p);
        CHECK(process_wait(p, 10000, &code));
        process_destroy(p);
    }
    CHECK(os_process_id() > 0);
    string_free(&err), string_free(&exe);
}

#ifndef _WIN32
// Gone, or a zombie its new parent has not reaped yet.
static bool process_gone(long long pid) {
    if (kill((pid_t)pid, 0) != 0) return errno == ESRCH;
    char path[64], stat[256] = { 0 };
    snprintf(path, sizeof(path), "/proc/%lld/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(stat, 1, sizeof(stat) - 1, f);
    fclose(f);
    const char *close_paren = n ? strrchr(stat, ')') : NULL;
    return close_paren && close_paren[1] == ' ' && close_paren[2] == 'Z';
}

// A child must not hold another child's pipes: closing one engine's stdin is EOF for it
// even while a later child runs (else an engine outlives a Gifu that crashed).
static void test_children_share_no_pipes(void) {
    String exe = self_program();
    StringView echo_args[] = { SV("--platform-child"), SV("echo") }, sleep_args[] = { SV("--platform-child"), SV("sleep") };
    ProcessSpec echo = { string_sv(&exe), echo_args, countof(echo_args), SV(""), NULL, 0 };
    ProcessSpec sleeper = { string_sv(&exe), sleep_args, countof(sleep_args), SV(""), NULL, 0 };
    Process *a = process_spawn(&echo, NULL), *b = process_spawn(&sleeper, NULL);
    CHECK(a != NULL && b != NULL);
    if (a && b) {
        process_close_stdin(a);
        i32 code = 0;
        CHECK(process_wait(a, 5000, &code) && code == 1);   // EOF: fgets fails
        CHECK(!process_wait(b, 0, &code));                  // while the other child still runs
    }
    process_destroy(a);
    process_destroy(b);
    string_free(&exe);
}

// SIGTERM (and crashes) stop the children before the process ends.
static void test_termination_stops_children(void) {
    String exe = self_program();
    StringView args[] = { SV("--platform-child"), SV("terminated") };
    ProcessSpec spec = { string_sv(&exe), args, countof(args), SV(""), NULL, 0 };
    Process *p = process_spawn(&spec, NULL);
    CHECK(p != NULL);
    if (!p) return;
    String line = read_line(p);
    long long grandchild = atoll(string_cstr(&line));
    CHECK(grandchild > 0);
    i32 code = 0;
    CHECK(process_wait(p, 5000, &code) && code == -1);   // ended by the signal
    bool gone = false;
    for (i32 i = 0; i < 300 && grandchild > 0 && !(gone = process_gone(grandchild)); ++i) os_sleep_ms(10);
    CHECK(gone);
    if (!gone && grandchild > 0) kill((pid_t)grandchild, SIGKILL);
    process_destroy(p);
    string_free(&line), string_free(&exe);
}
#endif

typedef struct Counter {
    Mutex lock;
    i32 value;
} Counter;

static i32 count_up(void *arg) {
    Counter *c = arg;
    for (i32 i = 0; i < 10000; ++i) {
        mutex_lock(&c->lock);
        c->value++;
        mutex_unlock(&c->lock);
    }
    return 0;
}

static void test_threads(void) {
    Counter c = { .value = 0 };
    mutex_init(&c.lock);
    Thread t[4];
    for (i32 i = 0; i < 4; ++i) CHECK(thread_start(&t[i], count_up, &c));
    for (i32 i = 0; i < 4; ++i) thread_join(&t[i]);
    CHECK(c.value == 40000);
    mutex_destroy(&c.lock);
    f64 t0 = os_time_seconds();
    os_sleep_ms(20);
    CHECK(os_time_seconds() - t0 >= 0.015);
}

static void test_encoding(void) {
    String out = { 0 };
    CHECK(text_to_utf8(SV("GB2312"), SV("\xc6\xe5"), &out) && sv_eq(string_sv(&out), SV("棋")));   // the same everywhere
    string_free(&out);
    CHECK(text_to_utf8(SV("Shift_JIS"), SV("\x8a\xfb"), &out) && sv_eq(string_sv(&out), SV("棋")));
    string_free(&out);
    CHECK(utf8_is_valid(SV("碁")) && !utf8_is_valid(SV("\xc6\xe5")));
}

void register_platform_tests(void) {
    test_register("platform/paths", test_paths);
    test_register("platform/files", test_files);
    test_register("platform/processes", test_processes);
#ifndef _WIN32
    test_register("platform/children_share_no_pipes", test_children_share_no_pipes);
    test_register("platform/termination_stops_children", test_termination_stops_children);
#endif
    test_register("platform/threads", test_threads);
    test_register("platform/encoding", test_encoding);
}
