// Portable parts of the platform layer (platform.h): paths, directory trees,
// launcher prefixes, text encoding names, and tool-based screen capture.
#include "platform/platform_internal.h"

#include "base/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void string_vec_free(StringVec *v) {
    vec_foreach(String, s, v) string_free(s);
    vec_free(v);
}

int platform_compare_strings(const void *a, const void *b) {
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

// ---- Trees ----------------------------------------------------------------------------------------

bool os_remove_tree(StringView path) {
    if (!os_dir_exists(path)) return os_remove(path) || !os_file_exists(path);
    StringVec names = { 0 };
    bool ok = true;
    if (os_list_dir(path, &names)) {
        vec_foreach(String, n, &names) {
            String child = path_join(path, string_sv(n));
            ok &= os_remove_tree(string_sv(&child));
            string_free(&child);
        }
    }
    string_vec_free(&names);
    return os_remove_dir(path) && ok;
}

// ---- Processes ------------------------------------------------------------------------------------

// `launcher... program args...` -> a plain spec (argv storage in `storage`).
ProcessSpec platform_apply_launcher(const ProcessSpec *spec, StringView *storage, isize capacity) {
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


// ---- Text encoding (names; conversion is per platform) ---------------------------------------------

bool utf8_is_valid(StringView text) {
    const u8 *p = (const u8 *)text.data;
    isize i = 0, n = text.len;
    while (i < n) {
        u8 c = p[i];
        isize len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (len == 0 || i + len > n) return false;
        if (len == 2 && c < 0xC2) return false;   // overlong
        for (isize k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
        }
        i += len;
    }
    return true;
}

Charset charset_from_label(StringView label) {
    char norm[32];
    i32 n = 0;
    for (isize i = 0; i < label.len && n < (i32)sizeof(norm) - 1; ++i) {
        char c = label.data[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) norm[n++] = c;
    }
    norm[n] = '\0';
    StringView s = sv(norm);
    if (sv_eq(s, SV("utf8"))) return CS_UTF8;
    if (sv_eq(s, SV("gb2312")) || sv_eq(s, SV("gbk")) || sv_eq(s, SV("gb18030")) || sv_eq(s, SV("cp936")) ||
        sv_eq(s, SV("euccn")))
        return CS_GB18030;
    if (sv_eq(s, SV("big5")) || sv_eq(s, SV("big5hkscs")) || sv_eq(s, SV("cp950"))) return CS_BIG5;
    if (sv_eq(s, SV("shiftjis")) || sv_eq(s, SV("sjis")) || sv_eq(s, SV("cp932")) || sv_eq(s, SV("windows31j")))
        return CS_SHIFT_JIS;
    if (sv_eq(s, SV("eucjp"))) return CS_EUC_JP;
    if (sv_eq(s, SV("euckr")) || sv_eq(s, SV("cp949")) || sv_eq(s, SV("ksc56011987")) || sv_eq(s, SV("uhc")))
        return CS_EUC_KR;
    if (sv_eq(s, SV("iso88591")) || sv_eq(s, SV("latin1")) || sv_eq(s, SV("cp1252")) || sv_eq(s, SV("windows1252")))
        return CS_LATIN1;
    return CS_UNKNOWN;
}

// ---- Screen capture: shared parts and external tools ------------------------------------------------

// Replaces *error (the caller may reuse one String across calls).
void capture_set_error(String *error, String message) {
    if (error) {
        string_free(error);
        *error = message;
    } else {
        string_free(&message);
    }
}


// Full-screen grab, then the rect cut out of it (tools that only do full screens).
bool capture_crop_to(RgbaImage *full, const ScreenRect *rect, RgbaImage *out, String *error) {
    if (!rect) {
        *out = *full;
        *full = (RgbaImage){ 0 };
        return true;
    }
    bool ok = rgba_image_crop(full, rect->x, rect->y, rect->width, rect->height, out);
    rgba_image_free(full);
    if (!ok) capture_set_error(error, string_from(SV("the area is outside the screen")));
    return ok;
}

// ---- External tools ---------------------------------------------------------------------------------

// Runs argv[0] with the rest as arguments; collects stdout.
bool capture_run_tool(const StringView *argv, isize argc, String *out, String *error) {
    ProcessSpec spec = { argv[0], argv + 1, argc - 1, SV(""), NULL, 0 };
    Process *p = process_spawn(&spec, error);
    if (!p) return false;
    process_close_stdin(p);
    char buffer[65536];
    for (isize n; (n = process_read_stdout(p, buffer, (isize)sizeof(buffer))) > 0;) string_push(out, sv_from_parts(buffer, n));
    i32 code = -1;
    if (!process_wait(p, 10000, &code)) process_kill(p), process_wait(p, -1, &code);
    process_destroy(p);
    if (code != 0) {
        capture_set_error(error, string_format(SV_FMT " failed (exit code %d)", SV_ARG(argv[0]), code));
        return false;
    }
    return true;
}

bool capture_decode_png(StringView bytes, RgbaImage *out, String *error, StringView source) {
    const char *why = NULL;
    if (png_decode(bytes, out, &why)) return true;
    capture_set_error(error, string_format(SV_FMT ": %s", SV_ARG(source), why ? why : "bad image"));
    return false;
}

bool capture_grab_tool(ScreenCapture *c, RgbaImage *full, String *error) {
    StringView argv[32];
    isize argc = 0;
    String file = { 0 }, bytes = { 0 };
    bool to_file = false;
    if (c->kind == CAPTURE_GRIM) {
        argv[argc++] = SV("grim"), argv[argc++] = SV("-t"), argv[argc++] = SV("png"), argv[argc++] = SV("-");
    } else {
        const char *dir = getenv("XDG_RUNTIME_DIR");
if (!dir) dir = getenv("TEMP");   // Windows
        if (!dir) dir = getenv("TMPDIR");   // macOS
        file = string_format("%s/gifu-capture-%u.png", dir ? dir : "/tmp", ++c->serial);
        StringView rest = string_sv(&c->command);
        for (StringView tok = sv_chop_token(&rest); tok.len && argc < (isize)countof(argv); tok = sv_chop_token(&rest)) {
            bool placeholder = sv_eq(tok, SV("%f"));
            to_file |= placeholder;
            argv[argc++] = placeholder ? string_sv(&file) : tok;
        }
        if (argc == 0) {
            string_free(&file);
            capture_set_error(error, string_from(SV("sync.capture_command is empty")));
            return false;
        }
    }
    bool ok = capture_run_tool(argv, argc, &bytes, error);
    if (ok && to_file) {
        string_clear(&bytes);
        ok = os_read_file(string_sv(&file), &bytes);
        os_remove(string_sv(&file));
        if (!ok) capture_set_error(error, string_format("the capture command wrote no %s", string_cstr(&file)));
    }
    ok = ok && capture_decode_png(string_sv(&bytes), full, error, argv[0]);
    string_free(&bytes);
    string_free(&file);
    return ok;
}
