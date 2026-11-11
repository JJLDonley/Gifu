// screen_capture.h - grabs the screen (or a part of it) as an RGBA image.
//
// Backends:
//   gdi      Windows (BitBlt of the virtual screen, every monitor)
//   x11      X11 sessions (XGetImage of the root window)
//   portal   Wayland: xdg-desktop-portal's Screenshot, through `gdbus`
//            (GNOME / KDE / wlroots portals; the desktop may ask once)
//   grim     Wayland, wlroots compositors (sway, Hyprland ...)
//   command  any tool that writes a PNG to stdout, or to "%f" (sync.capture_command)
// "auto" picks gdi on Windows; on Linux portal then grim in a Wayland session,
// else x11. Rectangles are desktop coordinates (Windows: the virtual screen, which
// can be negative left of / above the primary monitor; X11: the root window).
// Tool and portal backends grab the whole screen and cut the rectangle out.
// A ScreenCapture is used by one thread at a time.
#pragma once

#include "base/image.h"

typedef struct ScreenRect {
    i32 x, y, width, height;
} ScreenRect;

typedef struct ScreenCapture ScreenCapture;

ScreenCapture *screen_capture_open(StringView backend, StringView command, String *error);
// rect NULL = the whole screen.
bool screen_capture_grab(ScreenCapture *capture, const ScreenRect *rect, RgbaImage *out, String *error);
StringView screen_capture_name(const ScreenCapture *capture);
void screen_capture_close(ScreenCapture *capture);
// A flag the owner sets to abandon a capture that is waiting (e.g. for the desktop's
// answer to a portal request): board sync passes its quit flag.
void screen_capture_set_cancel(ScreenCapture *capture, const volatile bool *cancel);
