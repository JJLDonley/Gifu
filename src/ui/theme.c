#include "ui/theme.h"

#include "base/log.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// ---- Colors ------------------------------------------------------------------------

static i32 hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool color_parse(StringView text, Color *out) {
    text = sv_trim(text);
    if (text.len < 4 || text.data[0] != '#') return false;
    text = sv_drop(text, 1);
    u8 v[8];
    for (isize i = 0; i < text.len && i < 8; ++i) {
        i32 d = hex_digit(text.data[i]);
        if (d < 0) return false;
        v[i] = (u8)d;
    }
    if (text.len == 3) {
        *out = (Color){ (u8)(v[0] * 17), (u8)(v[1] * 17), (u8)(v[2] * 17), 255 };
    } else if (text.len == 6 || text.len == 8) {
        *out = (Color){ (u8)(v[0] * 16 + v[1]), (u8)(v[2] * 16 + v[3]), (u8)(v[4] * 16 + v[5]),
                        text.len == 8 ? (u8)(v[6] * 16 + v[7]) : 255 };
    } else {
        return false;
    }
    return true;
}

Color color_with_alpha(Color c, f32 alpha) {
    c.a = (u8)CLAMP(alpha * 255.0f, 0.0f, 255.0f);
    return c;
}

Color color_lerp(Color a, Color b, f32 t) {
    t = CLAMP(t, 0.0f, 1.0f);
    return (Color){ (u8)(a.r + (b.r - a.r) * t), (u8)(a.g + (b.g - a.g) * t), (u8)(a.b + (b.b - a.b) * t),
                    (u8)(a.a + (b.a - a.a) * t) };
}

// ---- Field tables (JSON key -> struct field) ---------------------------------------

static const struct {
    const char *key;
    usize offset;
} COLOR_FIELDS[] = {
#define F(name) { #name, offsetof(ThemeColors, name) }
    F(background), F(surface), F(surface_alt), F(border), F(text), F(text_muted), F(accent), F(accent_text),
    F(hover), F(danger), F(board), F(board_line), F(board_label), F(black_stone), F(white_stone),
    F(black_stone_edge), F(white_stone_edge), F(last_move), F(winrate_black), F(winrate_white), F(graph_line),
    F(score_line), F(candidate_best), F(candidate_good), F(candidate_bad), F(candidate_text),
#undef F
};

typedef struct EnumName {
    const char *name;
    i32 value;
} EnumName;

static const EnumName BACKGROUND_KINDS[] = { { "solid", BACKGROUND_SOLID }, { "image", BACKGROUND_IMAGE }, { "paper", BACKGROUND_PAPER } };
static const EnumName BACKGROUND_FITS[] = { { "cover", BACKGROUND_FIT_COVER }, { "stretch", BACKGROUND_FIT_STRETCH }, { "tile", BACKGROUND_FIT_TILE } };
static const EnumName BOARD_STYLES[] = { { "flat", BOARD_STYLE_FLAT }, { "wood", BOARD_STYLE_WOOD }, { "paper", BOARD_STYLE_PAPER }, { "image", BOARD_STYLE_IMAGE } };
static const EnumName STONE_STYLES[] = { { "flat", STONE_STYLE_FLAT }, { "glossy", STONE_STYLE_GLOSSY }, { "ink", STONE_STYLE_INK } };

static i32 enum_from_json(JsonValue v, const EnumName *names, isize count, i32 fallback) {
    StringView s = json_string(v, SV(""));
    for (isize i = 0; i < count; ++i) {
        if (sv_eq(s, sv(names[i].name))) return names[i].value;
    }
    if (s.len) log_warn("theme: unknown value '" SV_FMT "'", SV_ARG(s));
    return fallback;
}

static void apply_string(String *dst, JsonValue v) {
    if (!json_is_string(v)) return;
    string_free(dst);
    *dst = string_from(json_string(v, SV("")));
}

static void apply_color(Color *dst, JsonValue v) {
    Color c;
    if (json_is_string(v) && color_parse(json_string(v, SV("")), &c)) *dst = c;
}

// ---- Theme ----------------------------------------------------------------------------

Theme theme_fallback(void) {
    Theme t = { 0 };
    t.id = string_from(SV("light"));
    t.name = string_from(SV("Light"));
    ThemeColors *c = &t.colors;
    c->background = (Color){ 255, 255, 255, 255 };
    c->surface = (Color){ 255, 255, 255, 255 };
    c->surface_alt = (Color){ 244, 244, 244, 255 };
    c->border = (Color){ 227, 227, 227, 255 };
    c->text = (Color){ 17, 17, 17, 255 };
    c->text_muted = (Color){ 110, 110, 110, 255 };
    c->accent = (Color){ 17, 17, 17, 255 };
    c->accent_text = (Color){ 255, 255, 255, 255 };
    c->hover = (Color){ 0, 0, 0, 15 };
    c->danger = (Color){ 198, 40, 40, 255 };
    c->board = (Color){ 241, 241, 241, 255 };
    c->board_line = (Color){ 42, 42, 42, 255 };
    c->board_label = (Color){ 112, 112, 112, 255 };
    c->black_stone = (Color){ 18, 18, 18, 255 };
    c->white_stone = (Color){ 255, 255, 255, 255 };
    c->black_stone_edge = (Color){ 0, 0, 0, 255 };
    c->white_stone_edge = (Color){ 42, 42, 42, 255 };
    c->last_move = (Color){ 229, 57, 53, 255 };
    c->winrate_black = (Color){ 17, 17, 17, 255 };
    c->winrate_white = (Color){ 255, 255, 255, 255 };
    c->graph_line = (Color){ 17, 17, 17, 255 };
    c->score_line = (Color){ 158, 158, 158, 255 };
    c->candidate_best = (Color){ 30, 136, 229, 255 };
    c->candidate_good = (Color){ 67, 160, 71, 255 };
    c->candidate_bad = (Color){ 229, 57, 53, 255 };
    c->candidate_text = (Color){ 0, 0, 0, 255 };
    t.background.kind = BACKGROUND_SOLID;
    t.background.opacity = 1.0f;
    t.background.paper = (PaperParams){ 0.03f, 0.0f, 0.1f, 0, { 139, 90, 43, 255 }, 1 };
    t.board.style = BOARD_STYLE_FLAT;
    t.board.line_width = 1.0f;
    t.stones.style = STONE_STYLE_FLAT;
    t.shape.radius = 8;
    t.shape.border_width = 1;
    t.shape.gap = 12;
    t.shape.padding = 14;
    t.fonts.ui = string_from(SV("fonts/OpenSans-Regular.ttf"));
    t.fonts.ui_bold = string_from(SV("fonts/OpenSans-Semibold.ttf"));
    t.fonts.mono = string_from(SV("fonts/SourceCodePro-Regular.ttf"));
    t.background.image = string_from(SV(""));
    t.board.texture = string_from(SV(""));
    return t;
}

Theme theme_clone(const Theme *src) {
    Theme t = *src;
    t.id = string_from(string_sv(&src->id));
    t.name = string_from(string_sv(&src->name));
    t.background.image = string_from(string_sv(&src->background.image));
    t.board.texture = string_from(string_sv(&src->board.texture));
    t.fonts.ui = string_from(string_sv(&src->fonts.ui));
    t.fonts.ui_bold = string_from(string_sv(&src->fonts.ui_bold));
    t.fonts.mono = string_from(string_sv(&src->fonts.mono));
    return t;
}

void theme_free(Theme *t) {
    string_free(&t->id);
    string_free(&t->name);
    string_free(&t->background.image);
    string_free(&t->board.texture);
    string_free(&t->fonts.ui);
    string_free(&t->fonts.ui_bold);
    string_free(&t->fonts.mono);
}

void theme_apply_json(Theme *t, JsonValue root) {
    apply_string(&t->name, json_get(root, "name"));
    t->dark = json_bool(json_get(root, "dark"), t->dark);

    JsonValue colors = json_get(root, "colors");
    for (i32 i = 0; i < (i32)countof(COLOR_FIELDS); ++i) {
        apply_color((Color *)((u8 *)&t->colors + COLOR_FIELDS[i].offset), json_get(colors, COLOR_FIELDS[i].key));
    }

    JsonValue bg = json_get(root, "background");
    t->background.kind = enum_from_json(json_get(bg, "kind"), BACKGROUND_KINDS, countof(BACKGROUND_KINDS), t->background.kind);
    t->background.fit = enum_from_json(json_get(bg, "fit"), BACKGROUND_FITS, countof(BACKGROUND_FITS), t->background.fit);
    t->background.opacity = (f32)json_number(json_get(bg, "opacity"), t->background.opacity);
    apply_string(&t->background.image, json_get(bg, "image"));
    JsonValue paper = json_get(bg, "paper");
    PaperParams *p = &t->background.paper;
    p->grain = (f32)json_number(json_get(paper, "grain"), p->grain);
    p->fibers = (f32)json_number(json_get(paper, "fibers"), p->fibers);
    p->vignette = (f32)json_number(json_get(paper, "vignette"), p->vignette);
    p->stains = (i32)json_int(json_get(paper, "stains"), p->stains);
    p->seed = (u32)json_int(json_get(paper, "seed"), p->seed);
    apply_color(&p->stain_color, json_get(paper, "stain_color"));

    JsonValue board = json_get(root, "board");
    t->board.style = enum_from_json(json_get(board, "style"), BOARD_STYLES, countof(BOARD_STYLES), t->board.style);
    apply_string(&t->board.texture, json_get(board, "texture"));
    t->board.line_width = (f32)json_number(json_get(board, "line_width"), t->board.line_width);
    t->board.shadow = json_bool(json_get(board, "shadow"), t->board.shadow);

    JsonValue stones = json_get(root, "stones");
    t->stones.style = enum_from_json(json_get(stones, "style"), STONE_STYLES, countof(STONE_STYLES), t->stones.style);
    t->stones.shadow = (f32)json_number(json_get(stones, "shadow"), t->stones.shadow);

    JsonValue shape = json_get(root, "shape");
    t->shape.radius = (f32)json_number(json_get(shape, "radius"), t->shape.radius);
    t->shape.border_width = (f32)json_number(json_get(shape, "border_width"), t->shape.border_width);
    t->shape.gap = (f32)json_number(json_get(shape, "gap"), t->shape.gap);
    t->shape.padding = (f32)json_number(json_get(shape, "padding"), t->shape.padding);

    JsonValue fonts = json_get(root, "fonts");
    apply_string(&t->fonts.ui, json_get(fonts, "ui"));
    apply_string(&t->fonts.ui_bold, json_get(fonts, "ui_bold"));
    apply_string(&t->fonts.mono, json_get(fonts, "mono"));
}

// ---- Loading ----------------------------------------------------------------------------

typedef struct ThemeSource {
    String id;
    JsonDoc doc;
    bool resolving, resolved;
    isize theme_index;
} ThemeSource;
typedef Vec(ThemeSource) ThemeSourceVec;

static isize source_find(const ThemeSourceVec *sources, StringView id) {
    for (isize i = 0; i < sources->len; ++i) {
        if (sv_eq(string_sv(&sources->items[i].id), id)) return i;
    }
    return -1;
}

static void resolve(ThemeSourceVec *sources, isize index, ThemeVec *out) {
    ThemeSource *src = &sources->items[index];
    if (src->resolved) return;
    if (src->resolving) {
        log_warn("theme '%s': circular extends", string_cstr(&src->id));
        return;
    }
    src->resolving = true;
    JsonValue root = json_root(&src->doc);
    StringView base_id = json_string(json_get(root, "extends"), SV(""));
    Theme theme;
    isize base = base_id.len ? source_find(sources, base_id) : -1;
    if (base >= 0) {
        resolve(sources, base, out);
        src = &sources->items[index];   // vector may not move, but keep it explicit
    }
    if (base >= 0 && sources->items[base].resolved) {
        theme = theme_clone(&out->items[sources->items[base].theme_index]);
    } else {
        if (base_id.len) log_warn("theme '%s': unknown base '" SV_FMT "'", string_cstr(&src->id), SV_ARG(base_id));
        theme = theme_fallback();
    }
    string_free(&theme.id);
    theme.id = string_from(string_sv(&src->id));
    theme_apply_json(&theme, root);
    src->theme_index = out->len;
    vec_push(out, theme);
    src->resolving = false;
    src->resolved = true;
}

static int compare_theme_order(const void *a, const void *b) {
    // Built-in order first, then alphabetical.
    static const char *const ORDER[] = { "light", "dark", "cream", "coffee-paper" };
    const Theme *x = a, *y = b;
    i32 ix = 99, iy = 99;
    for (i32 i = 0; i < (i32)countof(ORDER); ++i) {
        if (sv_eq(string_sv(&x->id), sv(ORDER[i]))) ix = i;
        if (sv_eq(string_sv(&y->id), sv(ORDER[i]))) iy = i;
    }
    if (ix != iy) return ix - iy;
    return strcmp(string_cstr(&x->id), string_cstr(&y->id));
}

void themes_load(ThemeVec *out, const StringView *dirs, isize dir_count) {
    ThemeSourceVec sources = { 0 };
    for (isize d = 0; d < dir_count; ++d) {
        StringVec names = { 0 };
        if (!os_list_dir(dirs[d], &names)) continue;
        vec_foreach(String, name, &names) {
            StringView n = string_sv(name);
            if (!sv_ends_with(n, SV(".json"))) continue;
            String path = path_join(dirs[d], n);
            JsonDoc doc = { 0 };
            String err = { 0 };
            if (!json_load_file(string_sv(&path), &doc, &err)) {
                log_warn("theme: %s", string_cstr(&err));
                string_free(&err);
                string_free(&path);
                continue;
            }
            StringView id = sv_take(n, n.len - 5);
            isize existing = source_find(&sources, id);
            if (existing >= 0) {
                json_free(&sources.items[existing].doc);
                sources.items[existing].doc = doc;
            } else {
                vec_push(&sources, (ThemeSource){ .id = string_from(id), .doc = doc });
            }
            string_free(&path);
        }
        string_vec_free(&names);
    }
    for (isize i = 0; i < sources.len; ++i) resolve(&sources, i, out);
    vec_foreach(ThemeSource, s, &sources) {
        string_free(&s->id);
        json_free(&s->doc);
    }
    vec_free(&sources);
    if (out->len == 0) vec_push(out, theme_fallback());
    qsort(out->items, (usize)out->len, sizeof(Theme), compare_theme_order);
}

void themes_free(ThemeVec *themes) {
    vec_foreach(Theme, t, themes) theme_free(t);
    vec_free(themes);
}

isize themes_find(const ThemeVec *themes, StringView id) {
    for (isize i = 0; i < themes->len; ++i) {
        if (sv_eq(string_sv(&themes->items[i].id), id)) return i;
    }
    return -1;
}
