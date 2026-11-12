// Theme editor: edits a copy of the current theme live (the whole window and a
// preview panel follow every change), then saves it as a user theme
// (<config_dir>/themes/<id>.theme) or cancels back to the original.
#include "ui/ui.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct EditableColor {
    const char *name;     // key without "colors." (also the i18n suffix)
    const char *label;    // English fallback
    usize offset;         // in ThemeColors
    i32 group;
} EditableColor;

enum { GROUP_BOARD, GROUP_INTERFACE, GROUP_ANALYSIS, GROUP_COUNT };

#define C(name, label, group) { #name, label, offsetof(ThemeColors, name), group }
static const EditableColor COLORS[] = {
    C(board, "Board", GROUP_BOARD),
    C(board_line, "Grid lines", GROUP_BOARD),
    C(board_label, "Coordinates", GROUP_BOARD),
    C(black_stone, "Black stone", GROUP_BOARD),
    C(black_stone_edge, "Black stone edge", GROUP_BOARD),
    C(white_stone, "White stone", GROUP_BOARD),
    C(white_stone_edge, "White stone edge", GROUP_BOARD),
    C(last_move, "Last move marker", GROUP_BOARD),
    C(markup, "Markup (marks, labels)", GROUP_BOARD),
    C(markup_line, "Markup arrows", GROUP_BOARD),
    C(background, "Window background", GROUP_INTERFACE),
    C(surface, "Panels", GROUP_INTERFACE),
    C(surface_alt, "Panel insets", GROUP_INTERFACE),
    C(border, "Borders", GROUP_INTERFACE),
    C(text, "Text", GROUP_INTERFACE),
    C(text_muted, "Secondary text", GROUP_INTERFACE),
    C(accent, "Accent (buttons, tabs)", GROUP_INTERFACE),
    C(accent_text, "Text on accent", GROUP_INTERFACE),
    C(hover, "Hover highlight", GROUP_INTERFACE),
    C(danger, "Warnings", GROUP_INTERFACE),
    C(winrate_black, "Win rate: Black", GROUP_ANALYSIS),
    C(winrate_white, "Win rate: White", GROUP_ANALYSIS),
    C(graph_line, "Graph line", GROUP_ANALYSIS),
    C(score_line, "Score line", GROUP_ANALYSIS),
    C(candidate_best, "Best candidate", GROUP_ANALYSIS),
    C(candidate_good, "Good candidate", GROUP_ANALYSIS),
    C(candidate_bad, "Bad candidate", GROUP_ANALYSIS),
    C(candidate_text, "Candidate text", GROUP_ANALYSIS),
    C(human_policy, "Human moves", GROUP_ANALYSIS),
};
#undef C

static const char *const GROUP_KEYS[GROUP_COUNT] = { "theme_editor.group_board", "theme_editor.group_interface", "theme_editor.group_analysis" };
static const char *const GROUP_FALLBACK[GROUP_COUNT] = { "Board & stones", "Interface", "Analysis" };

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static Color *color_at(Theme *t, i32 index) { return (Color *)((u8 *)&t->colors + COLORS[index].offset); }

static void hex_of(Color c, char out[16]) {
    if (c.a == 255) snprintf(out, 16, "#%02X%02X%02X", c.r, c.g, c.b);
    else snprintf(out, 16, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
}

// The editor keeps hue/saturation/value of the selected colour itself, so the
// hue survives greys and blacks while dragging.
static void load_selection(ThemeEditor *e) {
    Color c = *color_at(&e->edit, e->selected);
    Vector3 hsv = ColorToHSV(c);
    e->hsv[0] = hsv.x;
    e->hsv[1] = hsv.y;
    e->hsv[2] = hsv.z;
    e->hsv[3] = (f32)c.a / 255.0f;
    char hex[16];
    hex_of(c, hex);
    text_edit_set(&e->hex, sv(hex));
}

static void changed(Ui *ui) { ui->theme_editor.version++; }   // board layer cache key

static void rebuild_background(Ui *ui, const Theme *t) {
    string_clear(&ui->background.built_for);
    String cache = path_join(string_sv(&ui->app->config_dir), SV("cache"));
    background_build(&ui->background, t, string_sv(&ui->app->assets_dir), string_sv(&cache));
    string_free(&cache);
}

void theme_editor_open(Ui *ui) {
    ThemeEditor *e = &ui->theme_editor;
    if (!e->open) {
        e->base_index = ui->theme_index;
        e->edit = theme_clone(ui->theme);
        bool custom = sv_find(string_sv(&e->edit.name), SV("(custom)")) >= 0 || sv_find(string_sv(&e->edit.id), SV("custom")) >= 0;
        String name = custom ? string_from(string_sv(&e->edit.name)) : string_format(SV_FMT " (custom)", SV_ARG(string_sv(&e->edit.name)));
        text_edit_set(&e->name, string_sv(&name));
        string_free(&name);
        e->selected = 0;
        e->group = GROUP_BOARD;
        e->drag = 0;
        e->open = true;
        ui->theme = &e->edit;   // live: everything draws with the copy
        load_selection(e);
        changed(ui);
    }
    ui->dialog = UI_DIALOG_THEME_EDITOR;
    ui->focus_id = 0;
    anim_set(&ui->anim, CLAY_ID("ModalDialog").id, 0.0f);
}

void theme_editor_cancel(Ui *ui) {
    ThemeEditor *e = &ui->theme_editor;
    if (!e->open) return;
    e->open = false;
    bool background_edited = e->background_stale;
    e->background_stale = false;
    theme_free(&e->edit);
    if (background_edited) string_clear(&ui->background.built_for);
    ui_apply_theme(ui, e->base_index);
    changed(ui);
}

static void slug(StringView name, char *out, isize cap) {
    isize n = 0;
    bool dash = false;
    for (isize i = 0; i < name.len && n < cap - 1; ++i) {
        char c = name.data[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) {
            if (dash && n > 0 && n < cap - 1) out[n++] = '-';
            out[n++] = c;
            dash = false;
        } else {
            dash = true;
        }
    }
    if (n == 0) n = (isize)snprintf(out, (size_t)cap, "custom");
    out[n] = '\0';
}

static void save(Ui *ui) {
    ThemeEditor *e = &ui->theme_editor;
    StringView name = sv_trim(string_sv(&e->name.text));
    if (name.len == 0) name = SV("Custom");
    char id[64];
    slug(name, id, (isize)sizeof(id));
    // Never shadow a shipped theme: user files with a built-in id would replace it.
    static const char *const BUILTIN[] = { "light", "dark", "cream", "coffee-paper" };
    for (isize i = 0; i < countof(BUILTIN); ++i) {
        if (strcmp(id, BUILTIN[i]) == 0) strncat(id, "-custom", sizeof(id) - strlen(id) - 1);
    }
    string_free(&e->edit.id);
    e->edit.id = string_from(sv(id));
    string_free(&e->edit.name);
    e->edit.name = string_from(name);

    String dir = path_join(string_sv(&ui->app->config_dir), SV("themes"));
    String file = string_format(SV_FMT "/%s.theme", SV_ARG(string_sv(&dir)), id);
    String text = theme_write(&e->edit, SV("Made with Gifu's theme editor (View -> Edit theme...)."));
    os_make_dirs(string_sv(&dir));
    bool ok = os_write_file(string_sv(&file), string_sv(&text));
    string_free(&text);
    if (!ok) {
        app_set_status(ui->app, "Could not write %s", string_cstr(&file));
        string_free(&file);
        string_free(&dir);
        return;
    }
    app_set_status(ui->app, "Theme saved: %s", string_cstr(&file));
    string_free(&file);
    string_free(&dir);
    // Reload the list (the new file included) and switch to it.
    e->open = false;
    e->background_stale = false;
    theme_free(&e->edit);
    ui_reload_themes(ui);
    string_clear(&ui->background.built_for);
    isize index = themes_find(&ui->themes, sv(id));
    ui_apply_theme(ui, index >= 0 ? index : 0);
    changed(ui);
    ui_close_dialog(ui);
}

// ---- Widgets -------------------------------------------------------------------------------------

typedef struct SliderDraw {
    ThemeEditor *e;
    i32 channel;   // 0 hue, 1 saturation, 2 value, 3 alpha
} SliderDraw;

static Color hsv_color(f32 h, f32 s, f32 v, f32 a) {
    Color c = ColorFromHSV(h, s, v);
    c.a = (u8)lroundf(CLAMP(a, 0.0f, 1.0f) * 255.0f);
    return c;
}

static void draw_slider(void *user, Rectangle r) {
    SliderDraw *d = user;
    const f32 *hsv = d->e->hsv;
    enum { STEPS = 48 };
    if (d->channel == 3) {   // checkerboard behind the alpha ramp
        f32 cell = r.height / 2.0f;
        for (i32 i = 0; (f32)i * cell < r.width; ++i) {
            for (i32 j = 0; j < 2; ++j) {
                Color k = ((i + j) & 1) ? (Color){ 200, 200, 200, 255 } : (Color){ 255, 255, 255, 255 };
                DrawRectangleRec((Rectangle){ r.x + (f32)i * cell, r.y + (f32)j * cell, fminf(cell, r.x + r.width - (r.x + (f32)i * cell)), cell }, k);
            }
        }
    }
    for (i32 i = 0; i < STEPS; ++i) {
        f32 t0 = (f32)i / STEPS, t1 = (f32)(i + 1) / STEPS;
        Color a, b;
        switch (d->channel) {
            case 0: a = hsv_color(t0 * 360.0f, 1, 1, 1), b = hsv_color(t1 * 360.0f, 1, 1, 1); break;
            case 1: a = hsv_color(hsv[0], t0, hsv[2], 1), b = hsv_color(hsv[0], t1, hsv[2], 1); break;
            case 2: a = hsv_color(hsv[0], hsv[1], t0, 1), b = hsv_color(hsv[0], hsv[1], t1, 1); break;
            default: a = hsv_color(hsv[0], hsv[1], hsv[2], t0), b = hsv_color(hsv[0], hsv[1], hsv[2], t1); break;
        }
        Rectangle seg = { r.x + r.width * t0, r.y, r.width * (t1 - t0) + 1.0f, r.height };
        DrawRectangleGradientH((i32)seg.x, (i32)seg.y, (i32)ceilf(seg.width), (i32)seg.height, a, b);
    }
    f32 value = d->channel == 0 ? hsv[0] / 360.0f : hsv[d->channel];
    f32 x = r.x + r.width * CLAMP(value, 0.0f, 1.0f);
    DrawRectangleRec((Rectangle){ x - 2.5f, r.y - 3.0f, 5.0f, r.height + 6.0f }, (Color){ 255, 255, 255, 255 });
    DrawRectangleLinesEx((Rectangle){ x - 2.5f, r.y - 3.0f, 5.0f, r.height + 6.0f }, 1.0f, (Color){ 0, 0, 0, 200 });
}

static void slider(Ui *ui, i32 channel, StringView label) {
    ThemeEditor *e = &ui->theme_editor;
    const ThemeColors *c = &ui->theme->colors;
    SliderDraw *d = arena_new(&ui->frame, SliderDraw);
    *d = (SliderDraw){ e, channel };
    Clay_ElementId id = CLAY_IDI("ThemeSlider", (u32)channel);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 4) } }) {
        ui_label(ui, label, FONT_UI, 12, c->text_muted);
        CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 16)) } },
               .custom = { .customData = ui_custom(ui, draw_slider, d) } }) {}
    }
    if (ui->mouse_pressed && Clay_PointerOver(id)) e->drag = channel + 1;
    Clay_ElementData data = Clay_GetElementData(id);
    if (e->drag == channel + 1 && ui->mouse_down && data.found) {
        f32 t = CLAMP((ui->mouse.x - data.boundingBox.x) / fmaxf(1.0f, data.boundingBox.width), 0.0f, 1.0f);
        if (channel == 0) e->hsv[0] = t * 360.0f;
        else e->hsv[channel] = t;
        Color *target = color_at(&e->edit, e->selected);
        Color next = hsv_color(e->hsv[0], e->hsv[1], e->hsv[2], e->hsv[3]);
        if (memcmp(target, &next, sizeof(Color)) != 0) {
            *target = next;
            char hex[16];
            hex_of(next, hex);
            text_edit_set(&e->hex, sv(hex));
            changed(ui);
            if (strcmp(COLORS[e->selected].name, "background") == 0) e->background_stale = true;
        }
    }
}

static void color_row(Ui *ui, i32 index) {
    ThemeEditor *e = &ui->theme_editor;
    const ThemeColors *c = &ui->theme->colors;
    Color value = *color_at(&e->edit, index);
    Clay_ElementId id = CLAY_IDI("ThemeColorRow", (u32)index);
    bool selected = index == e->selected, over = Clay_PointerOver(id);
    char key[64], hex[16];
    snprintf(key, sizeof(key), "theme_editor.color.%s", COLORS[index].name);
    hex_of(value, hex);
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                       .padding = { ui_u16(ui, 8), ui_u16(ui, 8), ui_u16(ui, 6), ui_u16(ui, 6) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(selected ? color_with_alpha(c->accent, 0.14f) : over ? c->hover : (Color){ 0 }),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 5)) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 22)), CLAY_SIZING_FIXED(ui_px(ui, 22)) } },
               .backgroundColor = color_to_clay(value), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)),
               .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
            ui_label(ui, tr(ui, key, COLORS[index].label), selected ? FONT_UI_BOLD : FONT_UI, 14, c->text);
        }
        ui_label(ui, sv(hex), FONT_MONO, 12, c->text_muted);
    }
    if (over && ui->mouse_pressed && !selected) {
        e->selected = index;
        load_selection(e);
    }
}

// ---- Preview -------------------------------------------------------------------------------------

typedef struct PreviewDraw {
    Ui *ui;
} PreviewDraw;

static void stone(const Theme *t, Vector2 p, f32 r, bool black) {
    const ThemeColors *c = &t->colors;
    if (t->stones.shadow > 0) DrawCircleV((Vector2){ p.x + r * 0.08f, p.y + r * 0.12f }, r, color_with_alpha(BLACK, t->stones.shadow * 0.6f));
    DrawCircleV(p, r, black ? c->black_stone : c->white_stone);
    DrawRing(p, r - 1.2f, r, 0, 360, 36, black ? c->black_stone_edge : c->white_stone_edge);
    if (t->stones.style == STONE_STYLE_GLOSSY) {
        DrawCircleV((Vector2){ p.x - r * 0.32f, p.y - r * 0.35f }, r * 0.32f, color_with_alpha(WHITE, black ? 0.22f : 0.5f));
    }
}

static void draw_preview_board(void *user, Rectangle r) {
    PreviewDraw *d = user;
    Ui *ui = d->ui;
    const Theme *t = ui->theme;
    const ThemeColors *c = &t->colors;
    f32 size = fminf(r.width, r.height);
    Rectangle b = { r.x + (r.width - size) * 0.5f, r.y, size, size };
    DrawRectangleRec(b, c->board);
    if (t->board.style == BOARD_STYLE_WOOD && ui->board_texture.id) {
        Texture2D tex = ui->board_texture;
        DrawTexturePro(tex, (Rectangle){ 0, 0, (f32)tex.width * 0.3f, (f32)tex.height * 0.3f }, b, (Vector2){ 0 }, 0, WHITE);
    }
    enum { N = 7 };
    f32 margin = size * 0.12f, cell = (size - 2 * margin) / (N - 1);
    f32 lw = fmaxf(1.0f, t->board.line_width * ui->scale);
    for (i32 i = 0; i < N; ++i) {
        f32 o = margin + cell * (f32)i;
        DrawLineEx((Vector2){ b.x + margin, b.y + o }, (Vector2){ b.x + size - margin, b.y + o }, lw, c->board_line);
        DrawLineEx((Vector2){ b.x + o, b.y + margin }, (Vector2){ b.x + o, b.y + size - margin }, lw, c->board_line);
        char lab[4];
        snprintf(lab, sizeof(lab), "%c", 'A' + i);
        font_draw_centered(ui->fonts, FONT_MONO, cell * 0.32f, sv(lab), (Vector2){ b.x + o, b.y + margin * 0.45f }, c->board_label);
        snprintf(lab, sizeof(lab), "%d", N - i);
        font_draw_centered(ui->fonts, FONT_MONO, cell * 0.32f, sv(lab), (Vector2){ b.x + margin * 0.45f, b.y + o }, c->board_label);
    }
    DrawCircleV((Vector2){ b.x + margin + cell * 3, b.y + margin + cell * 3 }, cell * 0.08f, c->board_line);
#define AT(cx, cy) ((Vector2){ b.x + margin + cell * (cx), b.y + margin + cell * (cy) })
    f32 sr = cell * 0.48f;
    stone(t, AT(2, 2), sr, true);
    stone(t, AT(3, 2), sr, false);
    stone(t, AT(2, 3), sr, false);
    stone(t, AT(4, 4), sr, true);
    stone(t, AT(4, 3), sr, false);
    DrawCircleV(AT(4, 3), sr * 0.28f, c->last_move);
    // Candidates (best / good / bad) with their win rate, and a human-move ring.
    struct { i32 x, y; Color col; const char *txt; } cands[] = {
        { 3, 4, c->candidate_best, "52" }, { 5, 3, c->candidate_good, "49" }, { 1, 4, c->candidate_bad, "41" } };
    for (isize i = 0; i < countof(cands); ++i) {
        Vector2 p = AT(cands[i].x, cands[i].y);
        DrawCircleV(p, sr, color_with_alpha(cands[i].col, 0.85f));
        font_draw_centered(ui->fonts, FONT_UI_BOLD, cell * 0.36f, sv(cands[i].txt), p, c->candidate_text);
    }
    DrawRing(AT(5, 5), sr * 0.6f, sr * 0.8f, 0, 360, 32, c->human_policy);
#undef AT
}

static void draw_preview_graph(void *user, Rectangle r) {
    PreviewDraw *d = user;
    const ThemeColors *c = &d->ui->theme->colors;
    DrawRectangleRec(r, color_with_alpha(c->surface_alt, 0.6f));
    f32 mid = r.y + r.height * 0.5f;
    DrawLineEx((Vector2){ r.x, mid }, (Vector2){ r.x + r.width, mid }, 1.0f, color_with_alpha(c->text_muted, 0.35f));
    Vector2 prev = { 0 }, prev_s = { 0 };
    for (i32 i = 0; i <= 40; ++i) {
        f32 t = (f32)i / 40.0f;
        Vector2 p = { r.x + r.width * t, mid - r.height * 0.32f * sinf(t * 6.0f) * (0.4f + t * 0.6f) };
        Vector2 s = { p.x, mid - r.height * 0.2f * sinf(t * 6.0f + 0.6f) };
        if (i) {
            DrawLineEx(prev_s, s, 1.5f * d->ui->scale, c->score_line);
            DrawLineEx(prev, p, 2.0f * d->ui->scale, c->graph_line);
        }
        prev = p;
        prev_s = s;
    }
}

static void preview(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    PreviewDraw *d = arena_new(&ui->frame, PreviewDraw);
    d->ui = ui;
    // A sample of the window: background, a panel with widgets, a board and a graph.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 10), .padding = CLAY_PADDING_ALL(ui_u16(ui, 12)) },
           .backgroundColor = color_to_clay(c->background), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 8)),
           .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
               .custom = { .customData = ui_custom(ui, draw_preview_board, d) } }) {}
        CLAY(ui_card(ui, CLAY_ID("ThemePreviewCard"))) {
            CLAY({ .layout = { .childGap = ui_u16(ui, 6) } }) {
                ui_button(ui, CLAY_ID("ThemePrevBtn1"), tr(ui, "theme_editor.sample_button", "Button"), BUTTON_NORMAL, false, true);
                ui_button(ui, CLAY_ID("ThemePrevBtn2"), tr(ui, "theme_editor.sample_selected", "Selected"), BUTTON_NORMAL, true, true);
                ui_button(ui, CLAY_ID("ThemePrevBtn3"), tr(ui, "theme_editor.sample_primary", "Primary"), BUTTON_PRIMARY, false, true);
            }
            ui_label(ui, tr(ui, "theme_editor.sample_text", "Text on a panel"), FONT_UI, 14, c->text);
            ui_label(ui, tr(ui, "theme_editor.sample_muted", "Secondary text · Visits 1,234"), FONT_UI, 12, c->text_muted);
            // Win-rate bar.
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 22)) } },
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)), .clip = { .horizontal = true },
                   .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(0.56f), CLAY_SIZING_GROW(0) }, .padding = { ui_u16(ui, 8), 0, 0, 0 },
                                   .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                       .backgroundColor = color_to_clay(c->winrate_black) }) {
                    ui_label(ui, SV("56.0%"), FONT_UI_BOLD, 12, c->winrate_white);
                }
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 0, ui_u16(ui, 8), 0, 0 },
                                   .childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } },
                       .backgroundColor = color_to_clay(c->winrate_white) }) {
                    ui_label(ui, SV("44.0%"), FONT_UI_BOLD, 12, c->winrate_black);
                }
            }
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 60)) } }, .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)),
                   .custom = { .customData = ui_custom(ui, draw_preview_graph, d) } }) {}
            ui_label(ui, tr(ui, "theme_editor.sample_warning", "Blunder: −12.4%"), FONT_UI_BOLD, 13, c->danger);
        }
    }
}

// ---- Dialog --------------------------------------------------------------------------------------

static void option_row(Ui *ui, StringView label, const char *const *names, const char *const *keys, i32 count, i32 *value, u32 base) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .padding = { 0, ui_u16(ui, 4), 0, 0 } } }) { ui_label(ui, label, FONT_UI, 13, c->text_muted); }
        for (i32 i = 0; i < count; ++i) {
            if (ui_button(ui, CLAY_IDI("ThemeOpt", base + (u32)i), tr(ui, keys[i], names[i]), BUTTON_NORMAL, *value == i, true) && *value != i) {
                *value = i;
                changed(ui);
            }
        }
    }
}

static void body(Ui *ui, void *user) {
    UNUSED(user);
    ThemeEditor *e = &ui->theme_editor;
    const ThemeColors *c = &ui->theme->colors;
    // Name and overall options.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 16),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 300)), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            ui_label(ui, tr(ui, "theme_editor.name", "Name"), FONT_UI, 13, c->text_muted);
            ui_text_input(ui, CLAY_ID("ThemeName"), &e->name, 14, 1, SV("My theme"));
        }
        if (ui_button(ui, CLAY_ID("ThemeDark"), tr(ui, "theme_editor.dark", "Dark theme"), BUTTON_NORMAL, e->edit.dark, true)) {
            e->edit.dark = !e->edit.dark;
            changed(ui);
        }
        static const char *const BOARD_NAMES[] = { "Flat", "Wood", "Paper" };
        static const char *const BOARD_KEYS[] = { "theme_editor.board_flat", "theme_editor.board_wood", "theme_editor.board_paper" };
        i32 board = (i32)e->edit.board.style;
        if (board <= BOARD_STYLE_PAPER) {
            option_row(ui, tr(ui, "theme_editor.board_style", "Board"), BOARD_NAMES, BOARD_KEYS, 3, &board, 0);
            e->edit.board.style = (BoardStyle)board;
        }
        static const char *const STONE_NAMES[] = { "Flat", "Glossy", "Ink" };
        static const char *const STONE_KEYS[] = { "theme_editor.stones_flat", "theme_editor.stones_glossy", "theme_editor.stones_ink" };
        i32 stones = (i32)e->edit.stones.style;
        option_row(ui, tr(ui, "theme_editor.stone_style", "Stones"), STONE_NAMES, STONE_KEYS, 3, &stones, 10);
        e->edit.stones.style = (StoneStyle)stones;
    }
    f32 page_h = fminf(ui_px(ui, 560), (f32)GetScreenHeight() * 0.66f);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(page_h) }, .childGap = ui_u16(ui, 16) } }) {
        // Colour list, by group.
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 330)), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                           .childGap = ui_u16(ui, 6) } }) {
            CLAY({ .layout = { .childGap = ui_u16(ui, 4), .padding = { 0, 0, 0, ui_u16(ui, 4) } },
                   .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
                for (i32 g = 0; g < GROUP_COUNT; ++g) {
                    if (ui_button(ui, CLAY_IDI("ThemeGroup", (u32)g), tr(ui, GROUP_KEYS[g], GROUP_FALLBACK[g]), BUTTON_GHOST, e->group == g, true)) {
                        e->group = g;
                    }
                }
            }
            CLAY({ .id = CLAY_ID("ThemeColorList"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                   .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
                for (i32 i = 0; i < (i32)countof(COLORS); ++i) {
                    if (COLORS[i].group == e->group) color_row(ui, i);
                }
            }
        }
        // Selected colour.
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 250)), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                           .childGap = ui_u16(ui, 12) } }) {
            char key[64];
            snprintf(key, sizeof(key), "theme_editor.color.%s", COLORS[e->selected].name);
            ui_label(ui, tr(ui, key, COLORS[e->selected].label), FONT_UI_BOLD, 16, c->text);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 70)) } },
                   .backgroundColor = color_to_clay(*color_at(&e->edit, e->selected)), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 8)),
                   .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
            slider(ui, 0, tr(ui, "theme_editor.hue", "Hue"));
            slider(ui, 1, tr(ui, "theme_editor.saturation", "Saturation"));
            slider(ui, 2, tr(ui, "theme_editor.value", "Brightness"));
            slider(ui, 3, tr(ui, "theme_editor.alpha", "Opacity"));
            ui_label(ui, tr(ui, "theme_editor.hex", "Hex (#RRGGBB or #RRGGBBAA)"), FONT_UI, 12, c->text_muted);
            TextInputResult r = ui_text_input(ui, CLAY_ID("ThemeHex"), &e->hex, 14, 1, SV("#RRGGBB"));
            if (r == TEXT_INPUT_CHANGED || r == TEXT_INPUT_COMMITTED) {
                Color parsed;
                if (color_parse(sv_trim(string_sv(&e->hex.text)), &parsed)) {
                    *color_at(&e->edit, e->selected) = parsed;
                    Vector3 hsv = ColorToHSV(parsed);
                    e->hsv[0] = hsv.x, e->hsv[1] = hsv.y, e->hsv[2] = hsv.z, e->hsv[3] = (f32)parsed.a / 255.0f;
                    changed(ui);
                    if (strcmp(COLORS[e->selected].name, "background") == 0) e->background_stale = true;
                }
            }
            if (ui_button(ui, CLAY_ID("ThemeReset"), tr(ui, "theme_editor.reset", "Reset to original"), BUTTON_GHOST, false, true)) {
                const Theme *base = &ui->themes.items[e->base_index];
                *color_at(&e->edit, e->selected) = *(const Color *)((const u8 *)&base->colors + COLORS[e->selected].offset);
                load_selection(e);
                changed(ui);
            }
        }
        preview(ui);
    }
    // Paper backgrounds are generated: rebuild once the drag ends, not every frame.
    if (!ui->mouse_down) {
        e->drag = 0;
        if (e->background_stale) {
            e->background_stale = false;
            rebuild_background(ui, &e->edit);
        }
    }
}

// The base theme's file in <config_dir>/themes, if it is a user theme (empty otherwise).
static String user_theme_file(const Ui *ui) {
    const Theme *base = &ui->themes.items[ui->theme_editor.base_index];
    String file = string_format(SV_FMT "/themes/" SV_FMT ".theme", SV_ARG(string_sv(&ui->app->config_dir)), SV_ARG(string_sv(&base->id)));
    if (!os_file_exists(string_sv(&file))) string_clear(&file);
    return file;
}

static void delete_theme(Ui *ui) {
    String file = user_theme_file(ui);
    if (file.len == 0) return;
    bool ok = os_remove(string_sv(&file));
    app_set_status(ui->app, ok ? "Theme deleted: %s" : "Could not delete %s", string_cstr(&file));
    string_free(&file);
    if (!ok) return;
    ThemeEditor *e = &ui->theme_editor;
    e->open = false;
    e->background_stale = false;
    theme_free(&e->edit);
    ui_reload_themes(ui);
    string_clear(&ui->background.built_for);
    isize index = themes_find(&ui->themes, SV("cream"));
    ui_apply_theme(ui, index >= 0 ? index : 0);
    changed(ui);
    ui_close_dialog(ui);
}

static void footer(Ui *ui, void *user) {
    UNUSED(user);
    const ThemeColors *c = &ui->theme->colors;
    String own = user_theme_file(ui);
    if (own.len && ui_button(ui, CLAY_ID("ThemeDelete"), tr(ui, "theme_editor.delete", "Delete this theme"), BUTTON_GHOST, false, true)) {
        string_free(&own);
        delete_theme(ui);
        return;
    }
    string_free(&own);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
        ui_label(ui, tr(ui, "theme_editor.hint", "Changes show everywhere right away; Save keeps them as a new theme."), FONT_UI, 12, c->text_muted);
    }
    if (ui_button(ui, CLAY_ID("ThemeCancel"), tr(ui, "label.cancel", "Cancel"), BUTTON_NORMAL, false, true)) ui_close_dialog(ui);
    if (ui_button(ui, CLAY_ID("ThemeSave"), tr(ui, "theme_editor.save", "Save theme"), BUTTON_PRIMARY, false, true)) save(ui);
}

void theme_editor_dialog(Ui *ui) {
    if (!ui->theme_editor.open) theme_editor_open(ui);
    ui_modal_shell(ui, tr(ui, "theme_editor.title", "Theme editor"), 1120, body, footer, NULL);
}
