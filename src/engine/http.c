#include "engine/http.h"

#include "base/os.h"
#include "engine/process.h"

#include <stdio.h>

static const StringView CURL = { "curl", 4 };

// Runs curl to completion, collecting stdout. Returns its exit code (or -1).
static i32 run_curl(const StringView *args, isize argc, String *out, String *err_text) {
    ProcessSpec spec = { CURL, args, argc, SV_NULL, NULL, 0 };
    String error = { 0 };
    Process *p = process_spawn(&spec, &error);
    if (!p) {
        if (err_text) *err_text = string_format("cannot run curl: %s", string_cstr(&error));
        string_free(&error);
        return -1;
    }
    char buffer[16384];
    for (isize n; (n = process_read_stdout(p, buffer, sizeof(buffer))) > 0;) {
        if (out) string_push(out, sv_from_parts(buffer, n));
    }
    String stderr_text = { 0 };
    for (isize n; (n = process_read_stderr(p, buffer, sizeof(buffer))) > 0;) string_push(&stderr_text, sv_from_parts(buffer, n));
    i32 code = -1;
    process_wait(p, -1, &code);
    process_destroy(p);
    if (err_text && code != 0) *err_text = string_format("curl failed (%d): %s", code, string_cstr(&stderr_text));
    string_free(&stderr_text);
    return code;
}

bool http_get(StringView url, String *body, String *error) {
    StringView args[] = { SV("-sSfL"), SV("--max-time"), SV("60"), SV("-A"), SV("CataGui"), url };
    string_clear(body);
    return run_curl(args, countof(args), body, error) == 0;
}

i64 http_content_length(StringView url) {
    StringView args[] = { SV("-sSfIL"), SV("--max-time"), SV("30"), SV("-A"), SV("CataGui"), url };
    String headers = { 0 };
    i64 size = -1;
    if (run_curl(args, countof(args), &headers, NULL) == 0) {
        // The last Content-Length wins (redirect hops come first).
        StringView rest = string_sv(&headers);
        while (rest.len) {
            StringView line = sv_trim(sv_chop_line(&rest));
            if (line.len > 15 && sv_eq_nocase(sv_take(line, 15), SV("content-length:"))) {
                i64 v;
                if (sv_parse_i64(sv_drop(line, 15), &v)) size = v;
            }
        }
    }
    string_free(&headers);
    return size;
}

bool http_download(StringView url, StringView dest, i64 total, HttpProgressFn progress, HttpCancelFn cancelled, void *user,
                   String *error) {
    String part = string_format(SV_FMT ".part", SV_ARG(dest));
    os_make_dirs(path_dirname(dest));
    os_remove(string_sv(&part));
    StringView args[] = { SV("-sSfL"), SV("--retry"), SV("3"), SV("-A"), SV("CataGui"), SV("-o"), string_sv(&part), url };
    ProcessSpec spec = { CURL, args, countof(args), SV_NULL, NULL, 0 };
    String spawn_error = { 0 };
    Process *p = process_spawn(&spec, &spawn_error);
    if (!p) {
        if (error) *error = string_format("cannot run curl: %s", string_cstr(&spawn_error));
        string_free(&spawn_error);
        string_free(&part);
        return false;
    }
    bool aborted = false;
    i32 code = -1;
    while (!process_wait(p, 250, &code)) {
        if (progress) progress(user, os_file_size(string_sv(&part)), total);
        if (cancelled && cancelled(user)) {
            process_kill(p);
            process_wait(p, 2000, &code);
            aborted = true;
            break;
        }
    }
    char buffer[4096];
    String stderr_text = { 0 };
    for (isize n; (n = process_read_stderr(p, buffer, sizeof(buffer))) > 0;) string_push(&stderr_text, sv_from_parts(buffer, n));
    process_destroy(p);
    bool ok = !aborted && code == 0 && os_rename(string_sv(&part), dest);
    if (!ok) {
        os_remove(string_sv(&part));
        if (error) *error = aborted ? string_from(SV("cancelled")) : string_format("download failed (%d): %s", code, string_cstr(&stderr_text));
    } else if (progress) {
        i64 size = os_file_size(dest);
        progress(user, size, size);
    }
    string_free(&stderr_text);
    string_free(&part);
    return ok;
}
