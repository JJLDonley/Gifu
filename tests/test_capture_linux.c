// Linux screen capture over D-Bus, against fake KWin and portal services on a
// private bus (dbus-daemon): the same method calls, replies, file descriptors and
// unicast signals as the real desktop services, without a desktop.
#include "test.h"

#include "platform/dbus_link.h"
#include "platform/platform.h"

#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 4x3 RGBA PNG: pixel (x, y) = (60x, 80y, 200, 255).
static const u8 TEST_PNG[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x03, 0x08, 0x06, 0x00, 0x00, 0x00, 0xb4, 0xf4, 0xae, 0xc6, 0x00, 0x00, 0x00, 0x2a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x15, 0xc8, 0x31,
    0x01, 0x00, 0x20, 0x10, 0xc4, 0xb0, 0x0a, 0x43, 0xc4, 0xcb, 0x39, 0x39, 0x88, 0x38, 0x11, 0xb8, 0x82, 0x32, 0x64, 0x09, 0x70, 0xee, 0x52,
    0x54, 0xc1, 0x18, 0x8a, 0x3a, 0x3f, 0xb6, 0xa1, 0xa8, 0x7a, 0xa4, 0x69, 0x1d, 0x4d, 0x19, 0x31, 0xc2, 0x4a, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

typedef enum FakeMode { FAKE_OK, FAKE_REFUSE, FAKE_SILENT } FakeMode;

typedef struct Fake {
    DbusLink *link;
    FakeMode kwin, portal;
    i32 workspace_w, workspace_h;   // CaptureWorkspace answers this size (0: refuse)
    i32 asked_w, asked_h;           // the last CaptureArea size requested
    volatile bool stop;
    String png_path;
} Fake;

// The test's private session bus (an own dbus-daemon), started once.
static LineChannel *g_bus;
static bool g_bus_ok;

static bool start_bus(void) {
    if (g_bus) return g_bus_ok;
    StringView args[] = { SV("--session"), SV("--nofork"), SV("--nopidfile"), SV("--print-address=1") };
    ProcessSpec spec = { SV("dbus-daemon"), args, countof(args), SV(""), NULL, 0 };
    String err = { 0 };
    g_bus = line_channel_start(&spec, &err);
    string_free(&err);
    if (!g_bus) return false;
    for (f64 until = os_time_seconds() + 5; os_time_seconds() < until;) {
        Line line;
        if (!line_channel_poll(g_bus, &line)) {
            os_sleep_ms(10);
            continue;
        }
        if (line.kind == LINE_STDOUT && sv_starts_with(string_sv(&line.text), SV("unix:"))) {
            setenv("DBUS_SESSION_BUS_ADDRESS", string_cstr(&line.text), 1);   // before libdbus first reads it
            g_bus_ok = true;
        }
        line_free(&line);
        if (g_bus_ok) break;
    }
    return g_bus_ok;
}

static void answer_kwin(Fake *f, DbusCall *call) {
    if (f->kwin == FAKE_REFUSE) {
        dbus_link_send(f->link, dbus_msg_error(call, SV("org.kde.KWin.ScreenShot2.Error.NoAuthorized"), SV("The process is not authorized")));
        return;
    }
    bool workspace = sv_eq(string_sv(&call->member), SV("CaptureWorkspace"));
    if (workspace && f->workspace_w <= 0) {
        dbus_link_send(f->link, dbus_msg_error(call, SV("org.freedesktop.DBus.Error.Failed"), SV("no")));
        return;
    }
    u32 w = workspace ? (u32)f->workspace_w : (u32)dbus_value_int(dbus_arg(&call->args, 2));
    u32 h = workspace ? (u32)f->workspace_h : (u32)dbus_value_int(dbus_arg(&call->args, 3));
    if (!workspace) f->asked_w = (i32)w, f->asked_h = (i32)h;
    const DbusValue *pipe = dbus_arg(&call->args, workspace ? 1 : 5);
    DbusMsg *r = dbus_msg_reply(call);
    dbus_msg_dict_begin(r);
    dbus_msg_dict_string(r, SV("type"), SV("raw"));
    dbus_msg_dict_uint32(r, SV("format"), 6);   // QImage::Format_ARGB32_Premultiplied: B G R A bytes
    dbus_msg_dict_uint32(r, SV("width"), w);
    dbus_msg_dict_uint32(r, SV("height"), h);
    dbus_msg_dict_uint32(r, SV("stride"), w * 4);
    dbus_msg_dict_end(r);
    dbus_link_send(f->link, r);
    // Like KWin: the pixels after the reply, then the pipe closes.
    u8 *px = malloc((usize)w * h * 4);
    for (u32 i = 0; i < w * h; ++i) px[i * 4 + 0] = 10, px[i * 4 + 1] = (u8)(i % 256), px[i * 4 + 2] = 30, px[i * 4 + 3] = 255;
    if (pipe && pipe->fd >= 0) {
        isize n = write(pipe->fd, px, (usize)w * h * 4);
        UNUSED(n);
    }
    free(px);
}

static void answer_portal(Fake *f, DbusCall *call) {
    StringView token = dbus_value_string(dbus_dict_get(dbus_arg(&call->args, 1), SV("handle_token")));
    // The request path is built from the caller's unique name, as the real portal does.
    String path = string_from(SV("/org/freedesktop/portal/desktop/request/"));
    StringView u = string_sv(&call->sender);
    if (u.len && u.data[0] == ':') u = sv_drop(u, 1);
    for (isize i = 0; i < u.len; ++i) string_push_char(&path, u.data[i] == '.' ? '_' : u.data[i]);
    string_push_char(&path, '/');
    string_push(&path, token);
    DbusMsg *r = dbus_msg_reply(call);
    dbus_msg_path(r, string_sv(&path));
    dbus_link_send(f->link, r);
    if (f->portal != FAKE_SILENT) {
        if (!os_write_file(string_sv(&f->png_path), sv_from_parts((const char *)TEST_PNG, (isize)sizeof(TEST_PNG)))) return;
        // Response goes to the caller only (destination set), like xdg-desktop-portal.
        DbusMsg *sig = dbus_msg_signal(string_sv(&path), SV("org.freedesktop.portal.Request"), SV("Response"), string_sv(&call->sender));
        dbus_msg_uint32(sig, f->portal == FAKE_OK ? 0 : 1);
        dbus_msg_dict_begin(sig);
        if (f->portal == FAKE_OK) {
            String uri = string_format("file://%s", string_cstr(&f->png_path));
            dbus_msg_dict_string(sig, SV("uri"), string_sv(&uri));
            string_free(&uri);
        }
        dbus_msg_dict_end(sig);
        dbus_link_send(f->link, sig);
    }
    string_free(&path);
}

static i32 fake_main(void *arg) {
    Fake *f = arg;
    while (!f->stop) {
        DbusCall call;
        if (!dbus_link_next_call(f->link, 100, &call)) continue;
        if (sv_eq(string_sv(&call.member), SV("CaptureArea")) || sv_eq(string_sv(&call.member), SV("CaptureWorkspace"))) answer_kwin(f, &call);
        else if (sv_eq(string_sv(&call.member), SV("Screenshot"))) answer_portal(f, &call);
        else dbus_link_send(f->link, dbus_msg_error(&call, SV("org.freedesktop.DBus.Error.UnknownMethod"), SV("no")));
        dbus_call_free(&call);
    }
    return 0;
}

static bool fake_start(Fake *f, Thread *t) {
    String err = { 0 };
    f->link = dbus_link_open_session(&err);
    CHECK(f->link != NULL);
    if (!f->link) {
        string_free(&err);
        return false;
    }
    CHECK(dbus_link_request_name(f->link, SV("org.kde.KWin.ScreenShot2"), &err));
    CHECK(dbus_link_request_name(f->link, SV("org.freedesktop.portal.Desktop"), &err));
    string_free(&err);
    f->png_path = path_join(SV(GIFU_TEST_OUTPUT_DIR), SV("fake-portal-shot.png"));
    return thread_start(t, fake_main, f);
}

static void fake_stop(Fake *f, Thread *t) {
    f->stop = true;
    thread_join(t);
    dbus_link_close(f->link);
    string_free(&f->png_path);
}

static void test_kwin_and_portal(void) {
    if (!dbus_link_available() || !start_bus()) {
        printf("    (skipped: no libdbus-1 / dbus-daemon)\n");
        return;
    }
    Fake f = { .kwin = FAKE_OK, .portal = FAKE_OK };
    Thread t;
    if (!fake_start(&f, &t)) return;
    String err = { 0 };
    ScreenRect rect = { 5, 6, 16, 9 };

    // KWin CaptureArea: raw pixels through the pipe, converted to RGBA.
    ScreenCapture *c = screen_capture_open(SV("kwin"), SV(""), &err);
    CHECK(c != NULL);
    RgbaImage img = { 0 };
    CHECK(screen_capture_grab(c, &rect, &img, &err));
    CHECK(img.width == 16 && img.height == 9);
    if (img.pixels) CHECK(img.pixels[0] == 30 && img.pixels[2] == 10 && img.pixels[4 * 5 + 1] == 5 && img.pixels[3] == 255);   // B G R A -> R G B A
    rgba_image_free(&img);

    // KWin says NoAuthorized (Plasma before 6.8): the same capture moves to the portal.
    f.kwin = FAKE_REFUSE;
    ScreenRect whole = { 0, 0, 4, 3 };   // the fake portal's whole screen
    bool fell_back = screen_capture_grab(c, &whole, &img, &err);
    CHECK(fell_back);
    CHECK_SV(screen_capture_name(c), SV("portal"));
    CHECK(img.width == 4 && img.height == 3);
    if (img.pixels) CHECK(img.pixels[4 * 1 + 0] == 60 && img.pixels[4 * 4 + 1] == 80);   // (1,0) and (0,1)
    CHECK(!os_file_exists(string_sv(&f.png_path)));   // the portal's file is removed
    rgba_image_free(&img);
    screen_capture_close(c);

    // The portal says no: an error, not a hang.
    f.portal = FAKE_REFUSE;
    c = screen_capture_open(SV("portal"), SV(""), &err);
    CHECK(!screen_capture_grab(c, NULL, &img, &err));
    CHECK(sv_find(string_sv(&err), SV("refused")) >= 0);
    screen_capture_close(c);

    // The portal never answers: the owner's cancel flag ends the wait at once.
    f.portal = FAKE_SILENT;
    volatile bool cancel = false;
    c = screen_capture_open(SV("portal"), SV(""), &err);
    screen_capture_set_cancel(c, &cancel);
    cancel = true;
    f64 t0 = os_time_seconds();
    CHECK(!screen_capture_grab(c, NULL, &img, &err));
    CHECK(os_time_seconds() - t0 < 2.0);
    screen_capture_close(c);

    string_free(&err);
    fake_stop(&f, &t);
}

// XWayland at the full resolution ("apply scaling themselves"): the area picked in
// Gifu's pixels is converted to KWin's logical pixels (here 200%).
static void test_kwin_scale(void) {
    if (!dbus_link_available() || !start_bus()) return;
    Display *d = XOpenDisplay(NULL);
    i32 xw = d ? DisplayWidth(d, DefaultScreen(d)) : 0, xh = d ? DisplayHeight(d, DefaultScreen(d)) : 0;
    if (d) XCloseDisplay(d);
    Fake f = { .kwin = FAKE_OK, .portal = FAKE_OK, .workspace_w = xw / 2, .workspace_h = xh / 2 };
    Thread t;
    if (!fake_start(&f, &t)) return;
    String err = { 0 };
    ScreenCapture *c = screen_capture_open(SV("kwin"), SV(""), &err);
    ScreenRect rect = { 100, 60, 400, 300 };
    RgbaImage img = { 0 };
    CHECK(screen_capture_grab(c, &rect, &img, &err));
    if (xw > 0) CHECK(f.asked_w == 200 && f.asked_h == 150);   // halved
    else CHECK(f.asked_w == 400 && f.asked_h == 300);          // no X screen: unchanged
    rgba_image_free(&img);
    screen_capture_close(c);
    string_free(&err);
    fake_stop(&f, &t);
}

void register_capture_linux_tests(void) {
    test_register("capture/kwin_scale", test_kwin_scale); test_register("capture/linux_dbus", test_kwin_and_portal); }
