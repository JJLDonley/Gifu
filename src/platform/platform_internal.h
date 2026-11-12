// platform_internal.h - shared between the platform implementation files only.
#pragma once

#include "platform/platform.h"

int platform_compare_strings(const void *a, const void *b);   // qsort over String

// POSIX systems name the modification time differently (st_mtim / st_mtimespec).
struct stat;
i64 platform_stat_mtime_ns(const struct stat *st);   // platform_linux.c / platform_macos.c

// Canonical charset family for a label (case/punctuation-insensitive); text_to_utf8 is per system.
typedef enum Charset { CS_UNKNOWN, CS_UTF8, CS_GB18030, CS_BIG5, CS_SHIFT_JIS, CS_EUC_JP, CS_EUC_KR, CS_LATIN1 } Charset;
Charset charset_from_label(StringView label);
// `launcher... program args...` -> a plain spec (argv kept in `storage`).
ProcessSpec platform_apply_launcher(const ProcessSpec *spec, StringView *storage, isize capacity);

// ---- Screen capture -----------------------------------------------------------------------------

typedef enum CaptureKind {
    CAPTURE_GDI, CAPTURE_X11, CAPTURE_PORTAL, CAPTURE_GRIM, CAPTURE_SCREENCAPTURE, CAPTURE_COMMAND, CAPTURE_KWIN, CAPTURE_KINDS
} CaptureKind;
extern const char *const CAPTURE_NAMES[CAPTURE_KINDS];   // "gdi", "x11", "portal", "grim", "screencapture", "command", "kwin"

struct ScreenCapture {
    CaptureKind kind;
    String command;          // CAPTURE_COMMAND: the user's tool ("%f" = output file)
    void *display;           // CAPTURE_X11: the capture thread's own connection (Display *)
    bool rootless;           // CAPTURE_X11: the root window cannot be read
    void *dbus;              // CAPTURE_KWIN / CAPTURE_PORTAL: the capture thread's D-Bus link (DbusLink *)
    const volatile bool *cancel;   // set by the owner to abandon a capture that waits (screen_capture_set_cancel)
    f32 area_scale;          // CAPTURE_KWIN: Gifu's (XWayland) pixels per KWin logical pixel; 0 = not measured yet
    u32 serial;
};

// Per system (platform_<os>.c):
CaptureKind capture_default_kind(StringView command);   // "auto"
bool capture_kind_available(CaptureKind kind);
bool capture_grab_native(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error);   // the system's own kinds
void capture_close_native(ScreenCapture *c);

// Shared helpers (platform_common.c).
void capture_set_error(String *error, String message);
bool capture_crop_to(RgbaImage *full, const ScreenRect *rect, RgbaImage *out, String *error);
bool capture_run_tool(const StringView *argv, isize argc, String *out, String *error);
bool capture_decode_png(StringView bytes, RgbaImage *out, String *error, StringView source);
bool capture_grab_tool(ScreenCapture *c, RgbaImage *full, String *error);   // CAPTURE_GRIM, CAPTURE_COMMAND
bool capture_on_path(const char *program);
