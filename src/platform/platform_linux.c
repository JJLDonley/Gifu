// Linux-only parts of the platform layer (platform.h): the program's folder
// (/proc), XDG config, xdg-open, zenity / kdialog dialogs, and screen capture
// (X11, the Wayland screenshot portal, grim).
#include "platform/platform_internal.h"

#include "base/log.h"
#include "platform/dbus_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

i64 platform_stat_mtime_ns(const struct stat *st) { return (i64)st->st_mtim.tv_sec * 1000000000LL + (i64)st->st_mtim.tv_nsec; }

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

static DbusLink *capture_link(ScreenCapture *c, String *error) {
    if (!c->dbus) c->dbus = dbus_link_open_session(error);
    return c->dbus;
}

static bool cancelled(const ScreenCapture *c) { return c->cancel && *c->cancel; }

// One Screenshot request on the capture's own D-Bus connection: the portal sends
// Response only to the connection that asked, and drops the request when that
// connection goes away. The match is added before the call (Request docs).
static bool capture_grab_portal(ScreenCapture *c, RgbaImage *full, String *error) {
    DbusLink *link = capture_link(c, error);
    if (!link) return false;
    char token[32];
    snprintf(token, sizeof(token), "gifu%u", ++c->serial);
    String path = dbus_portal_request_path(link, sv(token));
    String rule = string_format("type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'", string_cstr(&path));
    bool ok = dbus_link_add_match(link, string_sv(&rule), error);
    string_free(&rule);
    DbusValue reply = { 0 }, response = { 0 };
    if (ok) {
        DbusMsg *m = dbus_msg_method(SV("org.freedesktop.portal.Desktop"), SV("/org/freedesktop/portal/desktop"),
                                     SV("org.freedesktop.portal.Screenshot"), SV("Screenshot"));
        dbus_msg_string(m, SV(""));   // no parent window
        dbus_msg_dict_begin(m);
        dbus_msg_dict_string(m, SV("handle_token"), sv(token));
        dbus_msg_dict_bool(m, SV("interactive"), false);
        dbus_msg_dict_end(m);
        ok = dbus_link_call(link, m, 10000, &reply, error);
    }
    if (ok) {
        // Old portals may answer on another path than the predicted one.
        StringView handle = dbus_value_string(dbus_arg(&reply, 0));
        if (handle.len && !sv_eq(handle, string_sv(&path))) {
            string_free(&path);
            path = string_from(handle);
            String again = string_format("type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'", string_cstr(&path));
            dbus_link_add_match(link, string_sv(&again), NULL);
            string_free(&again);
        }
        ok = dbus_link_wait_signal(link, string_sv(&path), SV("org.freedesktop.portal.Request"), SV("Response"), 30000, c->cancel, &response);
        if (!ok) capture_set_error(error, string_from(cancelled(c) ? SV("cancelled") : SV("no answer from the screenshot portal")));
    }
    if (ok) {
        i64 code = dbus_value_int(dbus_arg(&response, 0));
        StringView uri = dbus_value_string(dbus_dict_get(dbus_arg(&response, 1), SV("uri")));
        if (code != 0 || !uri.len) {
            capture_set_error(error, string_format("the desktop refused the screenshot (portal response %lld)", (long long)code));
            ok = false;
        } else {
            String file = uri_to_path(uri), bytes = { 0 };
            ok = os_read_file(string_sv(&file), &bytes) && capture_decode_png(string_sv(&bytes), full, error, SV("portal"));
            os_remove(string_sv(&file));   // saved for this request only (KDE puts it in ~/Pictures)
            string_free(&bytes);
            string_free(&file);
        }
    }
    dbus_value_free(&reply);
    dbus_value_free(&response);
    string_free(&path);
    return ok;
}

// ---- KWin (KDE Plasma) -----------------------------------------------------------------------------

// org.kde.KWin.ScreenShot2.CaptureArea: KWin writes the area's raw pixels to a
// pipe we pass (format, width, height, stride in the reply) and leaves Gifu's own
// windows out. Plasma 6.8 dropped the per-app restriction on it; older versions
// answer NoAuthorized and Gifu falls back to the portal.
static bool read_pipe(int fd, u8 *dst, isize want, const ScreenCapture *c) {
    isize got = 0;
    f64 until = os_time_seconds() + 5.0;
    while (got < want) {
        struct pollfd p = { fd, POLLIN, 0 };
        int r = poll(&p, 1, 100);
        if (r < 0 && errno != EINTR) return false;
        if (cancelled(c) || os_time_seconds() > until) return false;
        if (r <= 0) continue;
        ssize_t n = read(fd, dst + got, (size_t)(want - got));
        if (n < 0 && errno != EINTR && errno != EAGAIN) return false;
        if (n == 0) break;   // KWin closed the pipe
        if (n > 0) got += n;
    }
    return got == want;
}

static bool capture_grab_kwin(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    DbusLink *link = capture_link(c, error);
    if (!link) return false;
    if (!rect) {
        capture_set_error(error, string_from(SV("KWin capture needs an area")));
        return false;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        capture_set_error(error, string_from(SV("pipe failed")));
        return false;
    }
    DbusMsg *m = dbus_msg_method(SV("org.kde.KWin.ScreenShot2"), SV("/org/kde/KWin/ScreenShot2"), SV("org.kde.KWin.ScreenShot2"), SV("CaptureArea"));
    dbus_msg_int32(m, rect->x);
    dbus_msg_int32(m, rect->y);
    dbus_msg_uint32(m, (u32)MAX(rect->width, 1));
    dbus_msg_uint32(m, (u32)MAX(rect->height, 1));
    dbus_msg_dict_begin(m);
    dbus_msg_dict_bool(m, SV("include-cursor"), false);
    dbus_msg_dict_end(m);
    dbus_msg_fd(m, fds[1]);
    DbusValue reply = { 0 };
    bool ok = dbus_link_call(link, m, 5000, &reply, error);
    close(fds[1]);   // the message carried its own copy: EOF comes when KWin closes it
    const DbusValue *res = dbus_arg(&reply, 0);
    i64 format = dbus_value_int(dbus_dict_get(res, SV("format")));
    i64 w = dbus_value_int(dbus_dict_get(res, SV("width"))), h = dbus_value_int(dbus_dict_get(res, SV("height")));
    i64 stride = dbus_value_int(dbus_dict_get(res, SV("stride")));
    // QImage::Format (qimage.h): 4 RGB32, 5 ARGB32, 6 ARGB32_Premultiplied (0xAARRGGBB words:
    // B G R A bytes on little-endian), 16 RGBX8888, 17 RGBA8888, 18 RGBA8888_Premultiplied.
    bool bgra = format == 4 || format == 5 || format == 6, rgba = format == 16 || format == 17 || format == 18;
    if (ok && (!sv_eq(dbus_value_string(dbus_dict_get(res, SV("type"))), SV("raw")) || (!bgra && !rgba) || w <= 0 || h <= 0 || stride < w * 4)) {
        capture_set_error(error, string_format("KWin sent an image Gifu cannot read (format %lld)", (long long)format));
        ok = false;
    }
    u8 *raw = NULL;
    if (ok) {
        raw = malloc((usize)(stride * h));
        if (!raw) abort();
        ok = read_pipe(fds[0], raw, stride * h, c);
        if (!ok) capture_set_error(error, string_from(SV("KWin sent no image")));
    }
    if (ok && !rgba_image_alloc(out, (i32)w, (i32)h)) ok = false;
    for (i64 y = 0; ok && y < h; ++y) {
        const u8 *src = raw + y * stride;
        u8 *dst = out->pixels + y * w * 4;
        for (i64 x = 0; x < w; ++x, src += 4, dst += 4) {
            dst[0] = bgra ? src[2] : src[0];
            dst[1] = src[1];
            dst[2] = bgra ? src[0] : src[2];
            dst[3] = 255;
        }
    }
    free(raw);
    close(fds[0]);
    dbus_value_free(&reply);
    return ok;
}

// ---- X11 -----------------------------------------------------------------------------------------

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

// "auto": the user's command; on Wayland KWin on KDE, grim on wlroots desktops
// (sway, Hyprland...), else the portal; on X11 the root window.
static bool desktop_is(const char *name) {
    const char *d = getenv("XDG_CURRENT_DESKTOP");
    return d && strstr(d, name) != NULL;
}

CaptureKind capture_default_kind(StringView command) {
    const char *session = getenv("XDG_SESSION_TYPE");
    bool wayland = (session && strcmp(session, "wayland") == 0) || getenv("WAYLAND_DISPLAY");
    if (command.len) return CAPTURE_COMMAND;
    if (!wayland) return CAPTURE_X11;
    if (desktop_is("KDE") && dbus_link_available()) return CAPTURE_KWIN;
    if (!desktop_is("GNOME") && capture_on_path("grim")) return CAPTURE_GRIM;
    if (dbus_link_available()) return CAPTURE_PORTAL;
    return CAPTURE_X11;   // XWayland windows only
}

bool capture_kind_available(CaptureKind kind) {
    return kind == CAPTURE_X11 || kind == CAPTURE_GRIM || ((kind == CAPTURE_PORTAL || kind == CAPTURE_KWIN) && dbus_link_available());
}

// Errors that mean "KWin will not do this for us" (Plasma before 6.8, no KWin).
static bool kwin_refused(StringView error) {
    return sv_find(error, SV("NoAuthorized")) >= 0 || sv_find(error, SV("ServiceUnknown")) >= 0 || sv_find(error, SV("UnknownMethod")) >= 0 ||
           sv_find(error, SV("not provided")) >= 0;
}

bool capture_grab_native(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    if (c->kind == CAPTURE_X11) return capture_grab_x11(c, rect, out, error);
    if (c->kind == CAPTURE_KWIN) {
        if (capture_grab_kwin(c, rect, out, error)) return true;
        if (cancelled(c) || !error || !kwin_refused(string_sv(error))) return false;
        log_info("screen capture: KWin refused (%s); using the screenshot portal", string_cstr(error));
        c->kind = CAPTURE_PORTAL;
        string_clear(error);
    }
    RgbaImage full;
    bool ok = c->kind == CAPTURE_PORTAL ? capture_grab_portal(c, &full, error) : capture_grab_tool(c, &full, error);
    return ok && capture_crop_to(&full, rect, out, error);
}

void capture_close_native(ScreenCapture *c) {
    if (c->display) XCloseDisplay((Display *)c->display);
    c->display = NULL;
    dbus_link_close(c->dbus);
    c->dbus = NULL;
}
