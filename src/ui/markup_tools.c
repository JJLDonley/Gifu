// Markup mode: the board is a drawing surface. Each tool edits the current
// node's markup (core/markup.c) or its setup stones; the toolbar's Markup row
// (layout.lisp) holds the tool buttons, the shading palette and the counts.
//
//   shapes / shade / eraser   click, or drag to paint (the first point decides add or remove)
//   shade                     Shift+drag: rectangle; double-click: the empty region
//   letters / numbers         click: next free label, or remove
//   arrows / lines            drag from one point to another (again to remove)
//   pen                       freehand, in the palette colour
//   black / white stones      setup stones (a new node after a move)
#include "ui/ui.h"

#include "core/markup.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TOOL_IDS[MARK_TOOLS] = {
    "markup.tool.black", "markup.tool.white", "markup.tool.triangle", "markup.tool.square", "markup.tool.circle",
    "markup.tool.cross", "markup.tool.letter", "markup.tool.number", "markup.tool.arrow", "markup.tool.line",
    "markup.tool.pen",   "markup.tool.shade", "markup.tool.eraser",
};
static const char *const TOOL_LABELS[MARK_TOOLS] = {
    "Black stone", "White stone", "Triangle", "Square", "Circle", "Cross", "Letter", "Number",
    "Arrow",       "Line",        "Pen",      "Shade area", "Eraser",
};

// ---- Palette ----------------------------------------------------------------------------------------

enum { PALETTE_MAX = 8 };

static i32 palette(const Ui *ui, u32 out[PALETTE_MAX]) {
    StringView rest = string_sv(&ui->app->config.markup_colors);
    i32 n = 0;
    while (rest.len && n < PALETTE_MAX) {
        StringView tok = sv_trim(sv_chop_char(&rest, ','));
        if (sv_starts_with(tok, SV("#"))) tok = sv_drop(tok, 1);
        u32 v = 0;
        bool ok = tok.len == 6;
        for (isize i = 0; i < tok.len && ok; ++i) {
            char c = tok.data[i];
            i32 d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            ok = d >= 0;
            v = v << 4 | (u32)MAX(d, 0);
        }
        if (ok) out[n++] = v;
    }
    if (n == 0) out[n++] = 0xe0503c;
    return n;
}

static void set_palette_color(Ui *ui, i32 index, u32 rgb) {
    u32 colors[PALETTE_MAX];
    i32 n = palette(ui, colors);
    if (index < 0 || index >= n) return;
    colors[index] = rgb;
    String text = { 0 };
    for (i32 i = 0; i < n; ++i) string_pushf(&text, i ? ",%06x" : "%06x", colors[i]);
    string_free(&ui->app->config.markup_colors);
    ui->app->config.markup_colors = text;
}

static u32 current_color(const Ui *ui) {
    u32 colors[PALETTE_MAX];
    i32 n = palette(ui, colors);
    return colors[CLAMP(ui->markup_color, 0, n - 1)];
}

static Color rgb_color(u32 rgb, u8 alpha) { return (Color){ (u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, alpha }; }

// ---- Editing ----------------------------------------------------------------------------------------

typedef struct Edit {
    GameNode *node;
    Markup m;
} Edit;

static void edit_begin(Ui *ui, Edit *e) {
    e->node = ui->app->tree->current;
    markup_read(e->node, e->node->board.width, e->node->board.height, &e->m);
}

static void edit_end(Ui *ui, Edit *e, bool changed) {
    if (changed) {
        markup_write(ui->app->tree, e->node, &e->m);
        app_mark_edited(ui->app);
    }
    markup_free(&e->m);
}

// One point of a paint drag (shapes, shading, eraser).
static void paint_point(Ui *ui, Point p) {
    MarkupDrag *d = &ui->markup_drag;
    Edit e;
    edit_begin(ui, &e);
    i32 i = p.y * e.m.width + p.x;
    bool changed = true;
    switch (ui->markup_tool) {
        case MARK_TOOL_TRIANGLE:
        case MARK_TOOL_SQUARE:
        case MARK_TOOL_CIRCLE:
        case MARK_TOOL_CROSS: {
            MarkShape shape = (MarkShape)(MARK_TRIANGLE + (ui->markup_tool - MARK_TOOL_TRIANGLE));
            e.m.shape[i] = d->erase ? (e.m.shape[i] == shape ? MARK_NONE : e.m.shape[i]) : shape;
            break;
        }
        case MARK_TOOL_SHADE:
            e.m.shaded[i] = !d->erase;
            e.m.shade[i] = d->erase ? 0 : current_color(ui);
            break;
        case MARK_TOOL_ERASER: markup_erase_at(&e.m, p); break;
        default: changed = false; break;
    }
    edit_end(ui, &e, changed);
}

static void shade_points(Ui *ui, const bool *which, bool erase) {
    Edit e;
    edit_begin(ui, &e);
    for (i32 i = 0; i < e.m.width * e.m.height; ++i) {
        if (!which[i]) continue;
        e.m.shaded[i] = !erase;
        e.m.shade[i] = erase ? 0 : current_color(ui);
    }
    edit_end(ui, &e, true);
}

static void press(Ui *ui, const BoardGeometry *g, Point p) {
    MarkupDrag *d = &ui->markup_drag;
    App *app = ui->app;
    f64 now = GetTime();
    bool double_click = point_eq(p, d->last_click_point) && now - d->last_click < 0.35;
    d->last_click = now, d->last_click_point = p;
    *d = (MarkupDrag){ .active = true, .start = p, .last = p, .last_click = d->last_click, .last_click_point = d->last_click_point };
    Edit e;
    switch (ui->markup_tool) {
        case MARK_TOOL_BLACK:
        case MARK_TOOL_WHITE: {
            // Setup stones go on a setup node: after a move, start one.
            GameNode *node = app->tree->current;
            if (node->has_move) {
                node = game_tree_append_child(app->tree, node);
                game_tree_goto(app->tree, node);
            }
            setup_toggle_stone(app->tree, node, p, ui->markup_tool == MARK_TOOL_BLACK ? STONE_BLACK : STONE_WHITE);
            game_tree_rebuild(app->tree, node);
            app_mark_edited(app);
            d->active = false;
            return;
        }
        case MARK_TOOL_LETTER:
        case MARK_TOOL_NUMBER: {
            edit_begin(ui, &e);
            MarkLabel *l = &e.m.label[p.y * e.m.width + p.x];
            if (l->text[0]) l->text[0] = '\0';
            else markup_next_label(&e.m, ui->markup_tool == MARK_TOOL_NUMBER, l->text);
            edit_end(ui, &e, true);
            d->active = false;
            return;
        }
        case MARK_TOOL_PEN:
            d->pen[0][0] = (ui->mouse.x - g->origin.x) / g->cell, d->pen[0][1] = (ui->mouse.y - g->origin.y) / g->cell;
            d->pen_count = 1;
            return;
        case MARK_TOOL_ARROW:
        case MARK_TOOL_LINE: return;   // on release
        case MARK_TOOL_SHADE:
            if (double_click) {   // the empty region (territory) under the point
                bool region[BOARD_MAX_POINTS];
                if (markup_empty_region(&app->tree->current->board, p, region) > 0) {
                    edit_begin(ui, &e);
                    bool all = true;
                    for (i32 i = 0; i < e.m.width * e.m.height; ++i) all &= !region[i] || (e.m.shaded[i] && e.m.shade[i] == current_color(ui));
                    markup_free(&e.m);
                    shade_points(ui, region, all);   // already that colour: clear it instead
                }
                d->active = false;
                return;
            }
            d->rect = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            edit_begin(ui, &e);
            {
                i32 i = p.y * e.m.width + p.x;
                d->erase = e.m.shaded[i] && e.m.shade[i] == current_color(ui);
            }
            markup_free(&e.m);
            if (!d->rect) paint_point(ui, p);
            return;
        default:   // shapes, eraser
            if (ui->markup_tool >= MARK_TOOL_TRIANGLE && ui->markup_tool <= MARK_TOOL_CROSS) {
                edit_begin(ui, &e);
                MarkShape shape = (MarkShape)(MARK_TRIANGLE + (ui->markup_tool - MARK_TOOL_TRIANGLE));
                d->erase = e.m.shape[p.y * e.m.width + p.x] == shape;
                markup_free(&e.m);
            }
            paint_point(ui, p);
            return;
    }
}

static void release(Ui *ui, Point p) {
    MarkupDrag *d = &ui->markup_drag;
    Edit e;
    switch (ui->markup_tool) {
        case MARK_TOOL_ARROW:
        case MARK_TOOL_LINE:
            if (!point_eq(p, d->start)) {
                edit_begin(ui, &e);
                if (ui->markup_tool == MARK_TOOL_ARROW) markup_toggle_segment(e.m.arrows, &e.m.arrow_count, d->start, p);
                else markup_toggle_segment(e.m.lines, &e.m.line_count, d->start, p);
                edit_end(ui, &e, true);
            }
            break;
        case MARK_TOOL_PEN:
            if (d->pen_count >= 2) {
                edit_begin(ui, &e);
                PenStroke *s = markup_add_stroke(&e.m, current_color(ui));
                if (s) {
                    s->count = MIN(d->pen_count, MARKUP_MAX_STROKE_POINTS);
                    memcpy(s->points, d->pen, sizeof(f32) * 2 * (usize)s->count);
                }
                edit_end(ui, &e, s != NULL);
            }
            break;
        case MARK_TOOL_SHADE:
            if (d->rect) {
                bool which[BOARD_MAX_POINTS] = { 0 };
                i32 w = ui->app->tree->current->board.width;
                for (i32 y = MIN(d->start.y, p.y); y <= MAX(d->start.y, p.y); ++y) {
                    for (i32 x = MIN(d->start.x, p.x); x <= MAX(d->start.x, p.x); ++x) which[y * w + x] = true;
                }
                shade_points(ui, which, d->erase);
            }
            break;
        default: break;
    }
    d->active = false;
}

void markup_tools_input(Ui *ui, const BoardGeometry *g) {
    MarkupDrag *d = &ui->markup_drag;
    bool on_board = CheckCollisionPointRec(ui->mouse, g->board);
    Point p = (Point){ (i8)CLAMP((i32)floorf((ui->mouse.x - g->origin.x) / g->cell + 0.5f), 0, g->width - 1),
                       (i8)CLAMP((i32)floorf((ui->mouse.y - g->origin.y) / g->cell + 0.5f), 0, g->height - 1) };
    if (ui->mouse_pressed && on_board) press(ui, g, p);
    if (!d->active) return;
    SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
    if (ui->markup_tool == MARK_TOOL_PEN) {
        f32 x = (ui->mouse.x - g->origin.x) / g->cell, y = (ui->mouse.y - g->origin.y) / g->cell;
        f32 lx = d->pen[d->pen_count - 1][0], ly = d->pen[d->pen_count - 1][1];
        if ((x - lx) * (x - lx) + (y - ly) * (y - ly) > 0.12f * 0.12f && d->pen_count < MARKUP_MAX_STROKE_POINTS) {
            d->pen[d->pen_count][0] = x, d->pen[d->pen_count][1] = y;
            d->pen_count++;
        }
    } else if (!point_eq(p, d->last)) {
        bool paints = ui->markup_tool >= MARK_TOOL_TRIANGLE && ui->markup_tool <= MARK_TOOL_CROSS;
        paints |= ui->markup_tool == MARK_TOOL_ERASER || (ui->markup_tool == MARK_TOOL_SHADE && !d->rect);
        if (paints) paint_point(ui, p);
    }
    d->last = p;
    if (ui->mouse_released || !ui->mouse_down) release(ui, p);
}

// What a drag would add, drawn over the board while it lasts.
void markup_tools_preview(Ui *ui, const BoardGeometry *g) {
    const MarkupDrag *d = &ui->markup_drag;
    if (!d->active) return;
    Color accent = color_with_alpha(ui->theme->colors.accent, 0.7f);
    Vector2 a = { g->origin.x + g->cell * (f32)d->start.x, g->origin.y + g->cell * (f32)d->start.y };
    Vector2 b = { g->origin.x + g->cell * (f32)d->last.x, g->origin.y + g->cell * (f32)d->last.y };
    switch (ui->markup_tool) {
        case MARK_TOOL_ARROW:
        case MARK_TOOL_LINE:
            if (!point_eq(d->start, d->last)) DrawLineEx(a, b, fmaxf(2.0f, g->cell * 0.08f), accent);
            break;
        case MARK_TOOL_PEN: {
            Color c = rgb_color(current_color(ui), 235);
            for (i32 k = 0; k + 1 < d->pen_count; ++k) {
                Vector2 p = { g->origin.x + g->cell * d->pen[k][0], g->origin.y + g->cell * d->pen[k][1] };
                Vector2 q = { g->origin.x + g->cell * d->pen[k + 1][0], g->origin.y + g->cell * d->pen[k + 1][1] };
                DrawLineEx(p, q, fmaxf(2.0f, g->cell * 0.09f), c);
            }
            break;
        }
        case MARK_TOOL_SHADE:
            if (d->rect) {
                Rectangle r = { fminf(a.x, b.x) - g->cell * 0.5f, fminf(a.y, b.y) - g->cell * 0.5f, fabsf(b.x - a.x) + g->cell, fabsf(b.y - a.y) + g->cell };
                DrawRectangleRec(r, rgb_color(current_color(ui), d->erase ? 40 : 90));
                DrawRectangleLinesEx(r, 2.0f, accent);
            }
            break;
        default: break;
    }
}

// ---- Toolbar pieces ---------------------------------------------------------------------------------

static void draw_swatch(void *user, Rectangle r) {
    const u32 *rgb = user;
    DrawRectangleRounded(r, 0.3f, 6, rgb_color(*rgb, 255));
}

typedef struct HsvSlider {
    f32 hsv[3];
    i32 channel;
} HsvSlider;

static void draw_hsv_slider(void *user, Rectangle r) {
    const HsvSlider *s = user;
    enum { STEPS = 36 };
    for (i32 i = 0; i < STEPS; ++i) {
        f32 t0 = (f32)i / STEPS, t1 = (f32)(i + 1) / STEPS;
        Color a, b;
        if (s->channel == 0) a = ColorFromHSV(t0 * 360.0f, 1, 1), b = ColorFromHSV(t1 * 360.0f, 1, 1);
        else if (s->channel == 1) a = ColorFromHSV(s->hsv[0], t0, s->hsv[2]), b = ColorFromHSV(s->hsv[0], t1, s->hsv[2]);
        else a = ColorFromHSV(s->hsv[0], s->hsv[1], t0), b = ColorFromHSV(s->hsv[0], s->hsv[1], t1);
        DrawRectangleGradientH((i32)(r.x + r.width * t0), (i32)r.y, (i32)ceilf(r.width * (t1 - t0)) + 1, (i32)r.height, a, b);
    }
    f32 v = s->channel == 0 ? s->hsv[0] / 360.0f : s->hsv[s->channel];
    f32 x = r.x + r.width * CLAMP(v, 0.0f, 1.0f);
    DrawRectangleRec((Rectangle){ x - 2.5f, r.y - 2.0f, 5.0f, r.height + 4.0f }, WHITE);
    DrawRectangleLinesEx((Rectangle){ x - 2.5f, r.y - 2.0f, 5.0f, r.height + 4.0f }, 1.0f, (Color){ 0, 0, 0, 200 });
}

// Edits the selected palette colour (opened by clicking the selected swatch again).
static void color_popover(Ui *ui, i32 index) {
    const ThemeColors *c = &ui->theme->colors;
    u32 rgb = current_color(ui);
    Vector3 hsv = ColorToHSV(rgb_color(rgb, 255));
    CLAY({ .id = CLAY_ID("MarkupColorPopover"),
           .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 220)), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .padding = CLAY_PADDING_ALL(ui_u16(ui, 10)), .childGap = ui_u16(ui, 8) },
           .backgroundColor = color_to_clay(c->surface),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius)),
           .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) },
           .floating = { .attachTo = CLAY_ATTACH_TO_PARENT,
                         .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
                         .offset = { 0, ui_px(ui, 6) },
                         .zIndex = 120 } }) {
        for (i32 ch = 0; ch < 3; ++ch) {
            HsvSlider *s = arena_new(&ui->frame, HsvSlider);
            *s = (HsvSlider){ { hsv.x, hsv.y, hsv.z }, ch };
            Clay_ElementId id = CLAY_IDI("MarkupHsv", (u32)ch);
            CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 14)) } },
                   .custom = { .customData = ui_custom(ui, draw_hsv_slider, s) } }) {}
            if (ui->mouse_pressed && Clay_PointerOver(id)) ui->markup_slider = ch + 1;
            if (ui->markup_slider == ch + 1 && ui->mouse_down) {
                Clay_BoundingBox b = Clay_GetElementData(id).boundingBox;
                f32 t = b.width > 0 ? CLAMP((ui->mouse.x - b.x) / b.width, 0.0f, 1.0f) : 0;
                f32 v[3] = { hsv.x, hsv.y, hsv.z };
                v[ch] = ch == 0 ? t * 359.9f : t;
                Color nc = ColorFromHSV(v[0], v[1], v[2]);
                set_palette_color(ui, index, (u32)nc.r << 16 | (u32)nc.g << 8 | nc.b);
            }
        }
    }
    if (!ui->mouse_down) ui->markup_slider = 0;
}

void markup_colors_element(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    u32 colors[PALETTE_MAX];
    i32 n = palette(ui, colors);
    ui->markup_color = CLAMP(ui->markup_color, 0, n - 1);
    CLAY({ .id = CLAY_ID("MarkupColors"), .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (i32 i = 0; i < n; ++i) {
            u32 *rgb = arena_new(&ui->frame, u32);
            *rgb = colors[i];
            Clay_ElementId id = CLAY_IDI("MarkupSwatch", (u32)i);
            bool selected = i == ui->markup_color;
            f32 size = ui_px(ui, 20);
            CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_FIXED(size + 6), CLAY_SIZING_FIXED(size + 6) }, .padding = CLAY_PADDING_ALL(3) },
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)),
                   .border = { .color = color_to_clay(selected ? c->accent : (Color){ 0 }), .width = CLAY_BORDER_OUTSIDE(2) } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .custom = { .customData = ui_custom(ui, draw_swatch, rgb) } }) {}
                if (selected && ui->markup_color_editing) color_popover(ui, i);
            }
            if (Clay_PointerOver(id) && ui->mouse_pressed && !ui->pointer_over_menu) {
                if (selected) ui->markup_color_editing = !ui->markup_color_editing;   // click again: edit the colour
                else ui->markup_color = i, ui->markup_color_editing = false;
                if (ui->markup_tool != MARK_TOOL_PEN) ui->markup_tool = MARK_TOOL_SHADE;   // picking a colour means painting
            }
        }
    }
    // A click elsewhere closes the colour editor.
    if (ui->markup_color_editing && ui->mouse_pressed && !Clay_PointerOver(CLAY_ID("MarkupColors")) &&
        !Clay_PointerOver(CLAY_ID("MarkupColorPopover"))) {
        ui->markup_color_editing = false;
    }
}

void markup_counts_element(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    const GameNode *node = ui->app->tree->current;
    Markup *m = arena_new(&ui->frame, Markup);
    markup_read(node, node->board.width, node->board.height, m);
    ShadeCount counts[PALETTE_MAX];
    i32 n = markup_shade_counts(m, &node->board, ui->app->config.markup_count_empty, counts, PALETTE_MAX);
    markup_free(m);
    CLAY({ .layout = { .childGap = ui_u16(ui, 8), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (i32 i = 0; i < n; ++i) {
            u32 *rgb = arena_new(&ui->frame, u32);
            *rgb = counts[i].rgb;
            CLAY({ .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 12)), CLAY_SIZING_FIXED(ui_px(ui, 12)) } },
                       .custom = { .customData = ui_custom(ui, draw_swatch, rgb) } }) {}
                CLAY_TEXT(ui_fmt(ui, "%d", counts[i].points), ui_text_style(ui, FONT_UI_BOLD, 13, c->text));
            }
        }
        if (n > 0) ui_command_button(ui, SV("markup.count_empty"), app_tr(ui->app, "markup.empty_only", "Empty only"), BUTTON_GHOST, 0);
    }
}

// ---- Commands ---------------------------------------------------------------------------------------

typedef struct ToolCommand {
    Ui *ui;
    MarkTool tool;
} ToolCommand;

static ToolCommand tool_commands[MARK_TOOLS];

static void cmd_tool(void *c) {
    ToolCommand *t = c;
    t->ui->markup_tool = t->tool;
    toolbar_switch(t->ui, SV("markup"));   // choosing a tool (menu, key) shows the Markup row
}
static bool tool_selected(const void *c) {
    const ToolCommand *t = c;
    return t->ui->markup_tool == t->tool;
}
static void cmd_clear(void *c) {
    Ui *ui = c;
    Edit e;
    edit_begin(ui, &e);
    bool changed = !markup_is_empty(&e.m);
    markup_clear(&e.m);
    edit_end(ui, &e, changed);
}
static void cmd_count_empty(void *c) {
    Ui *ui = c;
    ui->app->config.markup_count_empty = !ui->app->config.markup_count_empty;
}
static bool counting_empty(const void *c) { return ((const Ui *)c)->app->config.markup_count_empty; }
static void cmd_toolbar_next(void *c) { toolbar_next(c); }
static void cmd_toolbar_analysis(void *c) { toolbar_switch(c, SV("analysis")); }
static void cmd_toolbar_markup(void *c) { toolbar_switch(c, SV("markup")); }
static bool in_analysis(const void *c) { return !((const Ui *)c)->markup_mode; }
static bool in_markup(const void *c) { return ((const Ui *)c)->markup_mode; }

void markup_tools_register(Ui *ui) {
    CommandDef defs[MARK_TOOLS + 5];
    i32 n = 0;
    for (i32 t = 0; t < MARK_TOOLS; ++t) {
        tool_commands[t] = (ToolCommand){ ui, (MarkTool)t };
        defs[n++] = (CommandDef){ TOOL_IDS[t], TOOL_LABELS[t], TOOL_LABELS[t], cmd_tool, NULL, tool_selected, &tool_commands[t] };
    }
    defs[n++] = (CommandDef){ "markup.clear", "Clear marks", "Clear", cmd_clear, NULL, NULL, ui };
    defs[n++] = (CommandDef){ "markup.count_empty", "Count empty points only", "Empty only", cmd_count_empty, NULL, counting_empty, ui };
    defs[n++] = (CommandDef){ "toolbar.next", "Switch toolbar", "Toolbar", cmd_toolbar_next, NULL, NULL, ui };
    defs[n++] = (CommandDef){ "toolbar.analysis", "Analysis tools", "Analysis", cmd_toolbar_analysis, NULL, in_analysis, ui };
    defs[n++] = (CommandDef){ "toolbar.markup", "Markup tools", "Markup", cmd_toolbar_markup, NULL, in_markup, ui };
    command_register_all(&ui->app->commands, defs, n);
}
