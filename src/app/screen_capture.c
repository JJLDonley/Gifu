#include "app/screen_capture.h"

#include "base/log.h"
#include "base/os.h"
#include "engine/line_channel.h"
#include "engine/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

typedef enum CaptureKind { CAPTURE_GDI, CAPTURE_X11, CAPTURE_PORTAL, CAPTURE_GRIM, CAPTURE_COMMAND } CaptureKind;

struct ScreenCapture {
    CaptureKind kind;
    String command;          // CAPTURE_COMMAND
#ifndef _WIN32
    Display *display;        // CAPTURE_X11 (own connection: the capture thread's)
    bool rootless;           // the root window cannot be read: composite the X windows
#endif
    LineChannel *monitor;    // CAPTURE_PORTAL: `gdbus monitor` for the Response signals
    u32 serial;
};

// Replaces *error (the caller may reuse one String across calls).
static void set_error(String *error, String message) {
    if (error) {
        string_free(error);
        *error = message;
    } else {
        string_free(&message);
    }
}

static const char *const NAMES[] = { "gdi", "x11", "portal", "grim", "command" };

StringView screen_capture_name(const ScreenCapture *c) { return sv(NAMES[c->kind]); }

// Full-screen grab, then the rect cut out of it (tools that only do full screens).
static bool crop_to(RgbaImage *full, const ScreenRect *rect, RgbaImage *out, String *error) {
    if (!rect) {
        *out = *full;
        *full = (RgbaImage){ 0 };
        return true;
    }
    bool ok = rgba_image_crop(full, rect->x, rect->y, rect->width, rect->height, out);
    rgba_image_free(full);
    if (!ok) set_error(error, string_from(SV("the area is outside the screen")));
    return ok;
}

// ---- External tools ---------------------------------------------------------------------------------

// Runs argv[0] with the rest as arguments; collects stdout.
static bool run_tool(const StringView *argv, isize argc, String *out, String *error) {
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
        set_error(error, string_format(SV_FMT " failed (exit code %d)", SV_ARG(argv[0]), code));
        return false;
    }
    return true;
}

static bool decode_png(StringView bytes, RgbaImage *out, String *error, StringView source) {
    const char *why = NULL;
    if (png_decode(bytes, out, &why)) return true;
    set_error(error, string_format(SV_FMT ": %s", SV_ARG(source), why ? why : "bad image"));
    return false;
}

static bool grab_tool(ScreenCapture *c, RgbaImage *full, String *error) {
    StringView argv[32];
    isize argc = 0;
    String file = { 0 }, bytes = { 0 };
    bool to_file = false;
    if (c->kind == CAPTURE_GRIM) {
        argv[argc++] = SV("grim"), argv[argc++] = SV("-t"), argv[argc++] = SV("png"), argv[argc++] = SV("-");
    } else {
        const char *dir = getenv("XDG_RUNTIME_DIR");
#ifdef _WIN32
        if (!dir) dir = getenv("TEMP");
#endif
        file = string_format("%s/catagui-capture-%u.png", dir ? dir : "/tmp", ++c->serial);
        StringView rest = string_sv(&c->command);
        for (StringView tok = sv_chop_token(&rest); tok.len && argc < (isize)countof(argv); tok = sv_chop_token(&rest)) {
            bool placeholder = sv_eq(tok, SV("%f"));
            to_file |= placeholder;
            argv[argc++] = placeholder ? string_sv(&file) : tok;
        }
        if (argc == 0) {
            string_free(&file);
            set_error(error, string_from(SV("sync.capture_command is empty")));
            return false;
        }
    }
    bool ok = run_tool(argv, argc, &bytes, error);
    if (ok && to_file) {
        string_clear(&bytes);
        ok = os_read_file(string_sv(&file), &bytes);
        os_remove(string_sv(&file));
        if (!ok) set_error(error, string_format("the capture command wrote no %s", string_cstr(&file)));
    }
    ok = ok && decode_png(string_sv(&bytes), full, error, argv[0]);
    string_free(&bytes);
    string_free(&file);
    return ok;
}

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

static bool grab_portal(ScreenCapture *c, RgbaImage *full, String *error) {
    if (!c->monitor || !line_channel_alive(c->monitor)) {
        StringView args[] = { SV("monitor"), SV("--session"), SV("--dest"), SV("org.freedesktop.portal.Desktop") };
        ProcessSpec spec = { SV("gdbus"), args, countof(args), SV(""), NULL, 0 };
        if (c->monitor) line_channel_stop(c->monitor, SV(""));
        c->monitor = line_channel_start(&spec, error);
        if (!c->monitor) return false;
        os_sleep_ms(150);   // let it subscribe before the first request
    }
    char token[32], options[96];
    snprintf(token, sizeof(token), "catagui_%u_%u", (u32)os_time_seconds(), ++c->serial);
    snprintf(options, sizeof(options), "{'handle_token': <'%s'>, 'interactive': <false>}", token);
    StringView argv[] = { SV("gdbus"), SV("call"), SV("--session"), SV("--dest"), SV("org.freedesktop.portal.Desktop"),
                          SV("--object-path"), SV("/org/freedesktop/portal/desktop"), SV("--method"),
                          SV("org.freedesktop.portal.Screenshot.Screenshot"), SV(""), sv(options) };
    String reply = { 0 };
    bool ok = run_tool(argv, countof(argv), &reply, error);
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
            set_error(error, string_from(SV("the desktop refused the screenshot (portal)")));
            return false;
        }
        ok = os_read_file(string_sv(&path), &bytes) && decode_png(string_sv(&bytes), full, error, SV("portal"));
        os_remove(string_sv(&path));   // the portal saved it for this request only
        string_free(&bytes);
        string_free(&path);
        return ok;
    }
    set_error(error, string_from(SV("no answer from the screenshot portal")));
    return false;
}

// ---- Native -----------------------------------------------------------------------------------------

#ifdef _WIN32
static bool grab_gdi(const ScreenRect *rect, RgbaImage *out, String *error) {
    i32 vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    i32 vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    ScreenRect r = rect ? *rect : (ScreenRect){ vx, vy, vw, vh };   // desktop coordinates (may be negative)
    i32 x0 = MAX(r.x, vx), y0 = MAX(r.y, vy), x1 = MIN(r.x + r.width, vx + vw), y1 = MIN(r.y + r.height, vy + vh);
    if (x1 <= x0 || y1 <= y0 || !rgba_image_alloc(out, x1 - x0, y1 - y0)) {
        set_error(error, string_from(SV("the area is outside the screen")));
        return false;
    }
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = { .bmiHeader = { sizeof(BITMAPINFOHEADER), out->width, -out->height, 1, 32, BI_RGB } };
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    bool ok = bmp && bits;
    if (ok) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = BitBlt(mem, 0, 0, out->width, out->height, screen, x0, y0, SRCCOPY | CAPTUREBLT) != 0;
        GdiFlush();
        const u8 *src = bits;
        for (isize i = 0; ok && i < (isize)out->width * out->height; ++i) {   // BGRX -> RGBA
            out->pixels[i * 4 + 0] = src[i * 4 + 2];
            out->pixels[i * 4 + 1] = src[i * 4 + 1];
            out->pixels[i * 4 + 2] = src[i * 4 + 0];
            out->pixels[i * 4 + 3] = 255;
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    if (!ok) {
        rgba_image_free(out);
        set_error(error, string_from(SV("BitBlt failed")));
    }
    return ok;
}
#else
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

static bool grab_x11(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    if (!c->display) {
        XSetErrorHandler(on_x_error);
        c->display = XOpenDisplay(NULL);
    }
    if (!c->display) {
        set_error(error, string_from(SV("cannot open the X display")));
        return false;
    }
    Window root = DefaultRootWindow(c->display);
    XWindowAttributes attr;
    XGetWindowAttributes(c->display, root, &attr);
    ScreenRect r = rect ? *rect : (ScreenRect){ 0, 0, attr.width, attr.height };
    i32 x0 = MAX(r.x, 0), y0 = MAX(r.y, 0), x1 = MIN(r.x + r.width, attr.width), y1 = MIN(r.y + r.height, attr.height);
    if (x1 <= x0 || y1 <= y0 || !rgba_image_alloc(out, x1 - x0, y1 - y0)) {
        set_error(error, string_from(SV("the area is outside the screen")));
        return false;
    }
    if (!c->rootless) {
        x_error_code = 0;
        XImage *im = XGetImage(c->display, root, x0, y0, (unsigned)(x1 - x0), (unsigned)(y1 - y0), AllPlanes, ZPixmap);
        XSync(c->display, False);
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
    if (composite_windows(c->display, root, x0, y0, out)) return true;
    rgba_image_free(out);
    set_error(error, string_from(SV("no X window could be read here (on Wayland, use sync.capture=portal or grim)")));
    return false;
}
#endif

// ---- Interface --------------------------------------------------------------------------------------

#ifndef _WIN32
static bool on_path(const char *program) {
    const char *path = getenv("PATH");
    StringView rest = sv(path ? path : "");
    while (rest.len) {
        isize colon = sv_find_char(rest, ':');
        StringView dir = colon >= 0 ? sv_take(rest, colon) : rest;
        rest = colon >= 0 ? sv_drop(rest, colon + 1) : SV("");
        String full = path_join(dir, sv(program));
        bool found = os_file_exists(string_sv(&full));
        string_free(&full);
        if (found) return true;
    }
    return false;
}
#endif

ScreenCapture *screen_capture_open(StringView backend, StringView command, String *error) {
    CaptureKind kind = CAPTURE_GDI;
    if (backend.len == 0 || sv_eq(backend, SV("auto"))) {
#ifdef _WIN32
        kind = CAPTURE_GDI;
#else
        const char *session = getenv("XDG_SESSION_TYPE");
        bool wayland = session ? strcmp(session, "wayland") == 0 : false;
        if (command.len) kind = CAPTURE_COMMAND;
        else if (wayland && on_path("gdbus")) kind = CAPTURE_PORTAL;
        else if (wayland && on_path("grim")) kind = CAPTURE_GRIM;
        else kind = CAPTURE_X11;
#endif
    } else {
        bool found = false;
        for (i32 i = 0; i < (i32)countof(NAMES); ++i) {
            if (sv_eq(backend, sv(NAMES[i]))) kind = (CaptureKind)i, found = true;
        }
        if (!found) {
            set_error(error, string_format("unknown capture backend '" SV_FMT "'", SV_ARG(backend)));
            return NULL;
        }
    }
#ifdef _WIN32
    if (kind == CAPTURE_X11 || kind == CAPTURE_PORTAL || kind == CAPTURE_GRIM) {
#else
    if (kind == CAPTURE_GDI) {
#endif
        set_error(error, string_format("the %s capture is not available on this system", NAMES[kind]));
        return NULL;
    }
    ScreenCapture *c = calloc(1, sizeof(ScreenCapture));
    if (!c) abort();
    c->kind = kind;
    c->command = string_from(command);
    log_info("screen capture: %s", NAMES[kind]);
    return c;
}

bool screen_capture_grab(ScreenCapture *c, const ScreenRect *rect, RgbaImage *out, String *error) {
    *out = (RgbaImage){ 0 };
    switch (c->kind) {
#ifdef _WIN32
        case CAPTURE_GDI: return grab_gdi(rect, out, error);
#else
        case CAPTURE_X11: return grab_x11(c, rect, out, error);
#endif
        case CAPTURE_PORTAL:
        case CAPTURE_GRIM:
        case CAPTURE_COMMAND: {
            RgbaImage full;
            bool ok = c->kind == CAPTURE_PORTAL ? grab_portal(c, &full, error) : grab_tool(c, &full, error);
            return ok && crop_to(&full, rect, out, error);
        }
        default: return false;
    }
}

void screen_capture_close(ScreenCapture *c) {
    if (!c) return;
#ifndef _WIN32
    if (c->display) XCloseDisplay(c->display);
#endif
    if (c->monitor) line_channel_stop(c->monitor, SV(""));
    string_free(&c->command);
    free(c);
}
