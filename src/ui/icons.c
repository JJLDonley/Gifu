// Vector icons drawn with raylib primitives (crisp at any scale, no font
// dependency). Referenced from data as "@name" (layout.lisp :icon).
#include "ui/ui.h"

#include <math.h>

typedef void (*IconDraw)(Rectangle r, Color c, f32 stroke);

static Vector2 at(Rectangle r, f32 x, f32 y) { return (Vector2){ r.x + r.width * x, r.y + r.height * y }; }

static void line(Rectangle r, f32 x0, f32 y0, f32 x1, f32 y1, f32 w, Color c) {
    Vector2 a = at(r, x0, y0), b = at(r, x1, y1);
    DrawLineEx(a, b, w, c);
    DrawCircleV(a, w * 0.5f, c);   // round caps
    DrawCircleV(b, w * 0.5f, c);
}

static void chevron(Rectangle r, f32 cx, bool left, Color c, f32 w) {
    f32 d = left ? 0.16f : -0.16f;
    line(r, cx - d, 0.25f, cx + d, 0.5f, w, c);
    line(r, cx + d, 0.5f, cx - d, 0.75f, w, c);
}

static void icon_prev(Rectangle r, Color c, f32 w) { chevron(r, 0.5f, false, c, w); }
static void icon_next(Rectangle r, Color c, f32 w) { chevron(r, 0.5f, true, c, w); }
static void icon_prev2(Rectangle r, Color c, f32 w) { chevron(r, 0.38f, false, c, w), chevron(r, 0.66f, false, c, w); }
static void icon_next2(Rectangle r, Color c, f32 w) { chevron(r, 0.34f, true, c, w), chevron(r, 0.62f, true, c, w); }
static void icon_first(Rectangle r, Color c, f32 w) {
    chevron(r, 0.58f, false, c, w);
    line(r, 0.28f, 0.25f, 0.28f, 0.75f, w, c);
}
static void icon_last(Rectangle r, Color c, f32 w) {
    chevron(r, 0.42f, true, c, w);
    line(r, 0.72f, 0.25f, 0.72f, 0.75f, w, c);
}
static void icon_up(Rectangle r, Color c, f32 w) {
    line(r, 0.25f, 0.62f, 0.5f, 0.36f, w, c);
    line(r, 0.5f, 0.36f, 0.75f, 0.62f, w, c);
}
static void icon_down(Rectangle r, Color c, f32 w) {
    line(r, 0.25f, 0.38f, 0.5f, 0.64f, w, c);
    line(r, 0.5f, 0.64f, 0.75f, 0.38f, w, c);
}
static void icon_close(Rectangle r, Color c, f32 w) {
    line(r, 0.28f, 0.28f, 0.72f, 0.72f, w, c);
    line(r, 0.72f, 0.28f, 0.28f, 0.72f, w, c);
}
static void icon_plus(Rectangle r, Color c, f32 w) {
    line(r, 0.5f, 0.24f, 0.5f, 0.76f, w, c);
    line(r, 0.24f, 0.5f, 0.76f, 0.5f, w, c);
}
static void icon_stone_black(Rectangle r, Color c, f32 w) {
    UNUSED(w);
    DrawCircleV(at(r, 0.5f, 0.5f), r.width * 0.30f, c);
}
static void icon_stone_white(Rectangle r, Color c, f32 w) {
    DrawRing(at(r, 0.5f, 0.5f), r.width * 0.30f - w, r.width * 0.30f, 0, 360, 32, c);
}
static void icon_gear(Rectangle r, Color c, f32 w) {
    Vector2 o = at(r, 0.5f, 0.5f);
    f32 R = r.width * 0.30f;
    for (i32 i = 0; i < 8; ++i) {
        f32 a = (f32)i * PI / 4.0f;
        Vector2 p0 = { o.x + cosf(a) * R * 0.9f, o.y + sinf(a) * R * 0.9f };
        Vector2 p1 = { o.x + cosf(a) * R * 1.35f, o.y + sinf(a) * R * 1.35f };
        DrawLineEx(p0, p1, w * 1.6f, c);
    }
    DrawRing(o, R - w * 1.1f, R, 0, 360, 32, c);
}
static void icon_folder(Rectangle r, Color c, f32 w) {
    line(r, 0.2f, 0.32f, 0.42f, 0.32f, w, c);
    line(r, 0.42f, 0.32f, 0.5f, 0.4f, w, c);
    line(r, 0.5f, 0.4f, 0.8f, 0.4f, w, c);
    line(r, 0.8f, 0.4f, 0.8f, 0.72f, w, c);
    line(r, 0.8f, 0.72f, 0.2f, 0.72f, w, c);
    line(r, 0.2f, 0.72f, 0.2f, 0.32f, w, c);
}
static void icon_save(Rectangle r, Color c, f32 w) {
    line(r, 0.5f, 0.2f, 0.5f, 0.6f, w, c);
    line(r, 0.32f, 0.44f, 0.5f, 0.62f, w, c);
    line(r, 0.5f, 0.62f, 0.68f, 0.44f, w, c);
    line(r, 0.22f, 0.66f, 0.22f, 0.78f, w, c);
    line(r, 0.22f, 0.78f, 0.78f, 0.78f, w, c);
    line(r, 0.78f, 0.78f, 0.78f, 0.66f, w, c);
}
static void icon_play(Rectangle r, Color c, f32 w) {
    UNUSED(w);
    DrawTriangle(at(r, 0.34f, 0.24f), at(r, 0.34f, 0.76f), at(r, 0.76f, 0.5f), c);
}
static void icon_pause(Rectangle r, Color c, f32 w) {
    line(r, 0.38f, 0.26f, 0.38f, 0.74f, w * 1.3f, c);
    line(r, 0.62f, 0.26f, 0.62f, 0.74f, w * 1.3f, c);
}
static void icon_chart(Rectangle r, Color c, f32 w) {
    line(r, 0.2f, 0.7f, 0.4f, 0.48f, w, c);
    line(r, 0.4f, 0.48f, 0.56f, 0.6f, w, c);
    line(r, 0.56f, 0.6f, 0.8f, 0.3f, w, c);
}
static void icon_menu(Rectangle r, Color c, f32 w) {
    line(r, 0.24f, 0.3f, 0.76f, 0.3f, w, c);
    line(r, 0.24f, 0.5f, 0.76f, 0.5f, w, c);
    line(r, 0.24f, 0.7f, 0.76f, 0.7f, w, c);
}
static void icon_check(Rectangle r, Color c, f32 w) {
    line(r, 0.24f, 0.52f, 0.42f, 0.7f, w, c);
    line(r, 0.42f, 0.7f, 0.76f, 0.32f, w, c);
}

static const struct {
    const char *name;
    IconDraw draw;
} ICONS[] = {
    { "first", icon_first }, { "prev", icon_prev }, { "next", icon_next }, { "last", icon_last },
    { "prev2", icon_prev2 }, { "next2", icon_next2 }, { "up", icon_up }, { "down", icon_down },
    { "close", icon_close }, { "plus", icon_plus }, { "stone_black", icon_stone_black },
    { "stone_white", icon_stone_white }, { "gear", icon_gear }, { "folder", icon_folder }, { "save", icon_save },
    { "play", icon_play }, { "pause", icon_pause }, { "chart", icon_chart }, { "menu", icon_menu },
    { "check", icon_check }, { "chevron_down", icon_down },
};

i32 icon_find(StringView name) {
    if (sv_starts_with(name, SV("@"))) name = sv_drop(name, 1);
    for (i32 i = 0; i < (i32)countof(ICONS); ++i) {
        if (sv_eq(name, sv(ICONS[i].name))) return i;
    }
    return -1;
}

typedef struct IconPayload {
    i32 icon;
    Color color;
    f32 stroke;
} IconPayload;

static void draw_icon_element(void *user, Rectangle r) {
    IconPayload *p = user;
    if (p->icon >= 0) ICONS[p->icon].draw(r, p->color, p->stroke);
}

void icon_draw(i32 icon, Rectangle r, Color color, f32 stroke) {
    if (icon >= 0 && icon < (i32)countof(ICONS)) ICONS[icon].draw(r, color, stroke);
}

void ui_icon(Ui *ui, i32 icon, f32 size, Color color) {
    IconPayload *p = arena_new(&ui->frame, IconPayload);
    *p = (IconPayload){ icon, color, fmaxf(1.4f, ui_px(ui, 1.8f)) };
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, size)), CLAY_SIZING_FIXED(ui_px(ui, size)) } },
           .custom = { .customData = ui_custom(ui, draw_icon_element, p) } }) {}
}
