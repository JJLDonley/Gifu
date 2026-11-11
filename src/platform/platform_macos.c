// macOS-only parts of the platform layer (platform.h): the program's folder
// (from the Mach-O loader; inside Gifu.app that is Gifu.app/Contents/MacOS),
// ~/Library/Application Support, `open` for URLs, `osascript` dialogs and the
// system `screencapture` tool (the first capture asks for Screen Recording
// permission in System Settings).
#include "platform/platform_internal.h"

#include "base/log.h"

#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Folders, URLs --------------------------------------------------------------------------------

String os_executable_dir(void) {
    char raw[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return string_from(SV("."));
    const char *path = realpath(raw, real) ? real : raw;
    return string_from(path_dirname(sv(path)));
}

String os_config_dir(void) {
    const char *home = getenv("HOME");
    String support = path_join(sv(home ? home : "."), SV("Library/Application Support"));
    String out = path_join(string_sv(&support), SV("Gifu"));
    string_free(&support);
    return out;
}

const char *os_platform_name(void) { return "macos"; }

void os_open_url(StringView url) {
    StringView args[] = { url };
    os_spawn_detached(SV("/usr/bin/open"), args, 1);
}

// ---- File dialogs (AppleScript) -------------------------------------------------------------------

// Runs one AppleScript; its result (a POSIX path) without the trailing newline.
static bool run_osascript(StringView script, String *out) {
    StringView argv[] = { SV("/usr/bin/osascript"), SV("-e"), script };
    String text = { 0 }, error = { 0 };
    bool ok = capture_run_tool(argv, countof(argv), &text, &error);   // exit 1 = cancelled
    string_free(&error);
    StringView path = sv_trim(string_sv(&text));
    if (ok && path.len) *out = string_from(path);
    string_free(&text);
    return ok && out->len;
}

// AppleScript string literal of `s` (quotes and backslashes escaped).
static String apple_string(StringView s) {
    String out = string_from(SV("\""));
    for (isize i = 0; i < s.len; ++i) {
        if (s.data[i] == '"' || s.data[i] == '\\') string_push_char(&out, '\\');
        string_push_char(&out, s.data[i]);
    }
    string_push_char(&out, '"');
    return out;
}

bool file_dialog_available(void) { return true; }

bool file_dialog_open_sgf(StringView start_dir, String *out_path) {
    String dir = apple_string(start_dir);
    String script = start_dir.len ? string_format("POSIX path of (choose file with prompt \"Open SGF\" default location (POSIX file " SV_FMT "))",
                                                  SV_ARG(string_sv(&dir)))
                                  : string_from(SV("POSIX path of (choose file with prompt \"Open SGF\")"));
    bool ok = run_osascript(string_sv(&script), out_path);
    string_free(&script);
    string_free(&dir);
    return ok;
}

bool file_dialog_save_sgf(StringView suggested_path, String *out_path) {
    String name = apple_string(path_basename(suggested_path));
    String dir = apple_string(path_dirname(suggested_path));
    String script = string_format("POSIX path of (choose file name with prompt \"Save SGF\" default name " SV_FMT "%s" SV_FMT "%s)",
                                  SV_ARG(string_sv(&name)), path_dirname(suggested_path).len ? " default location (POSIX file " : "",
                                  SV_ARG(path_dirname(suggested_path).len ? string_sv(&dir) : SV("")), path_dirname(suggested_path).len ? ")" : "");
    bool ok = run_osascript(string_sv(&script), out_path);
    string_free(&script);
    string_free(&name);
    string_free(&dir);
    return ok;
}

// ---- Screen capture (screencapture) -----------------------------------------------------------------

CaptureKind capture_default_kind(StringView command) { return command.len ? CAPTURE_COMMAND : CAPTURE_SCREENCAPTURE; }
bool capture_kind_available(CaptureKind kind) { return kind == CAPTURE_SCREENCAPTURE; }

// screencapture -x (silent) -t png [-R x,y,w,h] FILE. Rectangles are in points; on
// a Retina display the image has 2 pixels per point.
bool capture_grab_native(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    const char *tmp = getenv("TMPDIR");
    String file = string_format("%s/gifu-capture-%u.png", tmp ? tmp : "/tmp", ++c->serial);
    char region[64] = "";
    if (rect) snprintf(region, sizeof(region), "%d,%d,%d,%d", rect->x, rect->y, rect->width, rect->height);
    StringView argv[8];
    isize argc = 0;
    argv[argc++] = SV("/usr/sbin/screencapture"), argv[argc++] = SV("-x"), argv[argc++] = SV("-t"), argv[argc++] = SV("png");
    if (rect) argv[argc++] = SV("-R"), argv[argc++] = sv(region);
    argv[argc++] = string_sv(&file);
    String ignored = { 0 }, bytes = { 0 };
    bool ok = capture_run_tool(argv, argc, &ignored, error);
    ok = ok && os_read_file(string_sv(&file), &bytes);
    if (ok) ok = capture_decode_png(string_sv(&bytes), out, error, SV("screencapture"));
    else if (error && !error->len) capture_set_error(error, string_from(SV("screencapture wrote no image (allow Gifu under Screen Recording)")));
    os_remove(string_sv(&file));
    string_free(&ignored);
    string_free(&bytes);
    string_free(&file);
    return ok;
}

void capture_close_native(ScreenCapture *c) { UNUSED(c); }
