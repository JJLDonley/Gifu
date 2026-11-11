// Screen capture front end (portable): picks the backend, runs tool backends,
// crops; each system's file grabs with its own APIs (capture_grab_native).
#include "platform/platform_internal.h"

#include "base/log.h"

#include <stdlib.h>

const char *const CAPTURE_NAMES[CAPTURE_KINDS] = { "gdi", "x11", "portal", "grim", "screencapture", "command" };

StringView screen_capture_name(const ScreenCapture *c) { return sv(CAPTURE_NAMES[c->kind]); }

ScreenCapture *screen_capture_open(StringView backend, StringView command, String *error) {
    CaptureKind kind = CAPTURE_COMMAND;
    if (backend.len == 0 || sv_eq(backend, SV("auto"))) {
        kind = capture_default_kind(command);
    } else {
        bool found = false;
        for (i32 i = 0; i < CAPTURE_KINDS; ++i) {
            if (sv_eq(backend, sv(CAPTURE_NAMES[i]))) kind = (CaptureKind)i, found = true;
        }
        if (!found) {
            capture_set_error(error, string_format("unknown capture backend '" SV_FMT "'", SV_ARG(backend)));
            return NULL;
        }
    }
    if (kind != CAPTURE_COMMAND && !capture_kind_available(kind)) {
        capture_set_error(error, string_format("the %s capture is not available on this system", CAPTURE_NAMES[kind]));
        return NULL;
    }
    ScreenCapture *c = calloc(1, sizeof(ScreenCapture));
    if (!c) abort();
    c->kind = kind;
    c->command = string_from(command);
    log_info("screen capture: %s", CAPTURE_NAMES[kind]);
    return c;
}

bool screen_capture_grab(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    *out = (RgbaImage){ 0 };
    if (c->kind == CAPTURE_COMMAND) {
        RgbaImage full;
        return capture_grab_tool(c, &full, error) && capture_crop_to(&full, rect, out, error);
    }
    return capture_grab_native(c, rect, out, error);
}

void screen_capture_close(ScreenCapture *c) {
    if (!c) return;
    capture_close_native(c);
    if (c->monitor) line_channel_stop(c->monitor, SV(""));
    string_free(&c->command);
    free(c);
}

bool capture_on_path(const char *program) {
    const char *path = getenv("PATH");
    StringView rest = sv(path ? path : "");
    while (rest.len) {
        isize sep = sv_find_char(rest, ':');
        StringView dir = sep >= 0 ? sv_take(rest, sep) : rest;
        rest = sep >= 0 ? sv_drop(rest, sep + 1) : SV("");
        String full = path_join(dir, sv(program));
        bool found = os_file_exists(string_sv(&full));
        string_free(&full);
        if (found) return true;
    }
    return false;
}
