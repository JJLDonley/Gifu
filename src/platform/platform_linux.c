// Linux-only parts of the platform layer (platform.h): the program's folder
// (/proc), XDG config, xdg-open, zenity / kdialog dialogs, and screen capture
// (X11, the Wayland screenshot portal, grim).
#include "platform/platform_internal.h"

#include "base/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

// ---- Folders, URLs --------------------------------------------------------------------------------

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

const char *os_platform_name(void) { return "linux"; }

void os_open_url(StringView url) {
    StringView args[] = { url };
    os_spawn_detached(SV("/usr/bin/xdg-open"), args, 1);
}

// ---- File dialogs (zenity / kdialog) ----------------------------------------------------------------


static bool have_program(const char *name) {
    String cmd = string_format("command -v %s >/dev/null 2>&1", name);
    int rc = system(string_cstr(&cmd));
    string_free(&cmd);
    return rc == 0;
}

// Single-quotes `s` for /bin/sh.
static void push_shell_quoted(String *cmd, StringView s) {
    string_push_char(cmd, '\'');
    for (isize i = 0; i < s.len; ++i) {
        if (s.data[i] == '\'') string_push(cmd, SV("'\\''"));
        else string_push_char(cmd, s.data[i]);
    }
    string_push_char(cmd, '\'');
}

static bool run_capture(String *cmd, String *out_path) {
    FILE *pipe = popen(string_cstr(cmd), "r");
    if (!pipe) return false;
    char buffer[4096];
    String result = { 0 };
    while (fgets(buffer, sizeof(buffer), pipe)) string_push_cstr(&result, buffer);
    int rc = pclose(pipe);
    StringView path = sv_trim(string_sv(&result));
    bool ok = rc == 0 && path.len > 0;
    if (ok) *out_path = string_from(path);
    string_free(&result);
    return ok;
}

bool file_dialog_available(void) { return have_program("zenity") || have_program("kdialog"); }

bool file_dialog_open_sgf(StringView start_dir, String *out_path) {
    String cmd = { 0 };
    if (have_program("zenity")) {
        string_push(&cmd, SV("zenity --file-selection --title='Open SGF' --file-filter='SGF files | *.sgf' --filename="));
        String dir = string_format(SV_FMT "/", SV_ARG(start_dir));
        push_shell_quoted(&cmd, string_sv(&dir));
        string_free(&dir);
    } else if (have_program("kdialog")) {
        string_push(&cmd, SV("kdialog --getopenfilename "));
        push_shell_quoted(&cmd, start_dir.len ? start_dir : SV("."));
        string_push(&cmd, SV(" 'SGF files (*.sgf)'"));
    } else {
        return false;
    }
    string_push(&cmd, SV(" 2>/dev/null"));
    bool ok = run_capture(&cmd, out_path);
    string_free(&cmd);
    return ok;
}

bool file_dialog_save_sgf(StringView suggested_path, String *out_path) {
    String cmd = { 0 };
    if (have_program("zenity")) {
        string_push(&cmd, SV("zenity --file-selection --save --confirm-overwrite --title='Save SGF' --filename="));
        push_shell_quoted(&cmd, suggested_path);
    } else if (have_program("kdialog")) {
        string_push(&cmd, SV("kdialog --getsavefilename "));
        push_shell_quoted(&cmd, suggested_path);
        string_push(&cmd, SV(" 'SGF files (*.sgf)'"));
    } else {
        return false;
    }
    string_push(&cmd, SV(" 2>/dev/null"));
    bool ok = run_capture(&cmd, out_path);
    string_free(&cmd);
    return ok;
}

// ---- Screen capture -------------------------------------------------------------------------------


// ---- xdg-desktop-portal (Wayland) -------------------------------------------------------------------

static String uri_to_path(StringView uri) {
    String path = { 0 };
    if (sv_starts_with(uri, SV("file://"))) uri = sv_drop(uri, 7);
    for (isize i = 0; i < uri.len; ++i) {
        char ch = uri.data[i];
        if (ch == '%' && i + 2 < uri.len) {
            char hex[3] = { uri.data[i + 1], uri.data[i + 2], 0 };
            ch = (char)strtol(hex, NULL, 16);
            i += 2;
        }
        string_push_char(&path, ch);
    }
    return path;
}

static bool capture_grab_portal(ScreenCapture *c, RgbaImage *full, String *error) {
    if (!c->monitor || !line_channel_alive(c->monitor)) {
        StringView args[] = { SV("monitor"), SV("--session"), SV("--dest"), SV("org.freedesktop.portal.Desktop") };
        ProcessSpec spec = { SV("gdbus"), args, countof(args), SV(""), NULL, 0 };
        if (c->monitor) line_channel_stop(c->monitor, SV(""));
        c->monitor = line_channel_start(&spec, error);
        if (!c->monitor) return false;
        os_sleep_ms(150);   // let it subscribe before the first request
    }
    char token[32], options[96];
    snprintf(token, sizeof(token), "gifu_%u_%u", (u32)os_time_seconds(), ++c->serial);
    snprintf(options, sizeof(options), "{'handle_token': <'%s'>, 'interactive': <false>}", token);
    StringView argv[] = { SV("gdbus"), SV("call"), SV("--session"), SV("--dest"), SV("org.freedesktop.portal.Desktop"),
                          SV("--object-path"), SV("/org/freedesktop/portal/desktop"), SV("--method"),
                          SV("org.freedesktop.portal.Screenshot.Screenshot"), SV(""), sv(options) };
    String reply = { 0 };
    bool ok = capture_run_tool(argv, countof(argv), &reply, error);
    string_free(&reply);
    if (!ok) return false;
    // Wait for ".../<token>: org.freedesktop.portal.Request.Response (uint32 0, {'uri': <'file:///...'>, ...})".
    char needle[48];
    snprintf(needle, sizeof(needle), "/%s:", token);
    for (f64 until = os_time_seconds() + 30.0; os_time_seconds() < until;) {
        Line line;
        if (!line_channel_poll(c->monitor, &line)) {
            if (!line_channel_alive(c->monitor)) break;
            os_sleep_ms(10);
            continue;
        }
        StringView text = string_sv(&line.text);
        bool mine = line.kind == LINE_STDOUT && sv_find(text, sv(needle)) >= 0 && sv_find(text, SV("Response")) >= 0;
        if (!mine) {
            line_free(&line);
            continue;
        }
        isize code_at = sv_find(text, SV("(uint32 "));
        bool granted = code_at >= 0 && sv_starts_with(sv_drop(text, code_at + 8), SV("0,"));
        isize uri_at = sv_find(text, SV("'uri': <'"));
        String path = { 0 }, bytes = { 0 };
        if (granted && uri_at >= 0) {
            StringView uri = sv_drop(text, uri_at + 9);
            isize end = sv_find(uri, SV("'>"));
            path = uri_to_path(end >= 0 ? sv_take(uri, end) : uri);
        }
        line_free(&line);
        if (!path.len) {
            capture_set_error(error, string_from(SV("the desktop refused the screenshot (portal)")));
            return false;
        }
        ok = os_read_file(string_sv(&path), &bytes) && capture_decode_png(string_sv(&bytes), full, error, SV("portal"));
        os_remove(string_sv(&path));   // the portal saved it for this request only
        string_free(&bytes);
        string_free(&path);
        return ok;
    }
    capture_set_error(error, string_from(SV("no answer from the screenshot portal")));
    return false;
}

static u8 channel(unsigned long pixel, unsigned long mask) {
    if (!mask) return 0;
    i32 shift = 0;
    while (!(mask & 1)) mask >>= 1, shift++;
    unsigned long v = (pixel >> shift) & mask;
    return (u8)(mask == 255 ? v : v * 255 / mask);
}

// Xlib's default error handler exits the process: record the error instead.
static volatile i32 x_error_code;
static int on_x_error(Display *display, XErrorEvent *event) {
    UNUSED(display);
    x_error_code = event->error_code;
    return 0;
}

// Copies `src` (an X image of size w x h) to `out` at (dx, dy), clipped.
static void blit_ximage(XImage *im, i32 w, i32 h, RgbaImage *out, i32 dx, i32 dy) {
    bool fast = im->bits_per_pixel == 32 && im->red_mask == 0xFF0000 && im->green_mask == 0xFF00 && im->blue_mask == 0xFF &&
                im->byte_order == LSBFirst;
    for (i32 y = 0; y < h; ++y) {
        i32 oy = dy + y;
        if (oy < 0 || oy >= out->height) continue;
        const u8 *row = (const u8 *)im->data + (usize)y * (usize)im->bytes_per_line;
        for (i32 x = 0; x < w; ++x) {
            i32 ox = dx + x;
            if (ox < 0 || ox >= out->width) continue;
            u8 *d = out->pixels + ((usize)oy * (usize)out->width + (usize)ox) * 4;
            if (fast) {
                d[0] = row[x * 4 + 2], d[1] = row[x * 4 + 1], d[2] = row[x * 4 + 0];
            } else {
                unsigned long px = XGetPixel(im, x, y);
                d[0] = channel(px, im->red_mask), d[1] = channel(px, im->green_mask), d[2] = channel(px, im->blue_mask);
            }
            d[3] = 255;
        }
    }
}

// Rootless X servers (XWayland) cannot read the root window. Each mapped
// top-level window can still be read: paint them bottom to top. Native Wayland
// windows are not visible this way (use the portal there).
static bool composite_windows(Display *dpy, Window root, i32 x0, i32 y0, RgbaImage *out) {
    Window root_ret, parent, *children = NULL;
    unsigned count = 0;
    if (!XQueryTree(dpy, root, &root_ret, &parent, &children, &count)) return false;
    bool any = false;
    for (unsigned i = 0; i < count; ++i) {
        XWindowAttributes a;
        x_error_code = 0;
        if (!XGetWindowAttributes(dpy, children[i], &a) || a.map_state != IsViewable || a.class != InputOutput) continue;
        i32 wx = a.x - x0, wy = a.y - y0;   // in the output image
        i32 cx0 = MAX(0, -wx), cy0 = MAX(0, -wy), cx1 = MIN(a.width, out->width - wx), cy1 = MIN(a.height, out->height - wy);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        XImage *im = XGetImage(dpy, children[i], cx0, cy0, (unsigned)(cx1 - cx0), (unsigned)(cy1 - cy0), AllPlanes, ZPixmap);
        XSync(dpy, False);
        if (!im || x_error_code) {
            if (im) XDestroyImage(im);
            continue;
        }
        blit_ximage(im, cx1 - cx0, cy1 - cy0, out, wx + cx0, wy + cy0);
        XDestroyImage(im);
        any = true;
    }
    if (children) XFree(children);
    return any;
}

static bool capture_grab_x11(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    if (!c->display) {
        XSetErrorHandler(on_x_error);
        c->display = XOpenDisplay(NULL);
    }
    if (!c->display) {
        capture_set_error(error, string_from(SV("cannot open the X display")));
        return false;
    }
    Window root = DefaultRootWindow((Display *)c->display);
    XWindowAttributes attr;
    XGetWindowAttributes((Display *)c->display, root, &attr);
    ScreenRect r = rect ? *rect : (ScreenRect){ 0, 0, attr.width, attr.height };
    i32 x0 = MAX(r.x, 0), y0 = MAX(r.y, 0), x1 = MIN(r.x + r.width, attr.width), y1 = MIN(r.y + r.height, attr.height);
    if (x1 <= x0 || y1 <= y0 || !rgba_image_alloc(out, x1 - x0, y1 - y0)) {
        capture_set_error(error, string_from(SV("the area is outside the screen")));
        return false;
    }
    if (!c->rootless) {
        x_error_code = 0;
        XImage *im = XGetImage((Display *)c->display, root, x0, y0, (unsigned)(x1 - x0), (unsigned)(y1 - y0), AllPlanes, ZPixmap);
        XSync((Display *)c->display, False);
        if (im && !x_error_code) {
            blit_ximage(im, out->width, out->height, out, 0, 0);
            XDestroyImage(im);
            return true;
        }
        if (im) XDestroyImage(im);
        c->rootless = true;   // e.g. XWayland: read the windows one by one from now on
        log_info("screen capture: the root window cannot be read (rootless X server); reading X windows");
    }
    for (isize i = 0; i < (isize)out->width * out->height; ++i) out->pixels[i * 4 + 3] = 255;   // uncovered: black
    if (composite_windows((Display *)c->display, root, x0, y0, out)) return true;
    rgba_image_free(out);
    capture_set_error(error, string_from(SV("no X window could be read here (on Wayland, use sync.capture=portal or grim)")));
    return false;
}

// ---- Screen capture: this system's backends -------------------------------------------------------

CaptureKind capture_default_kind(StringView command) {
    const char *session = getenv("XDG_SESSION_TYPE");
    bool wayland = session && strcmp(session, "wayland") == 0;
    if (command.len) return CAPTURE_COMMAND;
    if (wayland && capture_on_path("gdbus")) return CAPTURE_PORTAL;
    if (wayland && capture_on_path("grim")) return CAPTURE_GRIM;
    return CAPTURE_X11;
}

bool capture_kind_available(CaptureKind kind) { return kind == CAPTURE_X11 || kind == CAPTURE_PORTAL || kind == CAPTURE_GRIM; }

bool capture_grab_native(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    if (c->kind == CAPTURE_X11) return capture_grab_x11(c, rect, out, error);
    RgbaImage full;
    bool ok = c->kind == CAPTURE_PORTAL ? capture_grab_portal(c, &full, error) : capture_grab_tool(c, &full, error);
    return ok && capture_crop_to(&full, rect, out, error);
}

void capture_close_native(ScreenCapture *c) {
    if (c->display) XCloseDisplay((Display *)c->display);
    c->display = NULL;
}
