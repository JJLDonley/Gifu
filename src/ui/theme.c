#include "ui/theme.h"

#include "base/kv.h"
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
    F(score_line), F(candidate_best), F(candidate_good), F(candidate_bad), F(candidate_text), F(human_policy),
#undef F
};

typedef struct EnumName {
    const char *name;
    i32 value;
} EnumName;

static const EnumName BACKGROUND_KINDS[] = { { "solid", BACKGROUND_SOLID }, { "image", BACKGROUND_IMAGE }, { "paper", BACKGROUND_PAPER } };
static const EnumName BACKGROUND_FITS[] = { { "cover", BACKGROUND_FIT_COVER }, { "stretch", BACKGROUND_FIT_STRETCH }, { "tile", BACKGROUND_FIT_TILE } };
static const EnumName BOARD_STYLES[] = { { "flat", BOARD_STYLE_FLAT }, { "wood", BOARD_STYLE_WOOD }, { "paper", BOARD_STYLE_PAPER }, { "image", BOARD_STYLE_IMAGE },
                                          { "neon", BOARD_STYLE_NEON } };
static const EnumName STONE_STYLES[] = { { "flat", STONE_STYLE_FLAT }, { "glossy", STONE_STYLE_GLOSSY }, { "ink", STONE_STYLE_INK }, { "neon", STONE_STYLE_NEON } };

static bool enum_from_text(StringView s, const EnumName *names, isize count, i32 *out) {
    for (isize i = 0; i < count; ++i) {
        if (sv_eq(s, sv(names[i].name))) {
            *out = names[i].value;
            return true;
        }
    }
    return false;
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
    c->score_line = (Color){ 112, 112, 112, 255 };   // readable as text on surface_alt too
    c->candidate_best = (Color){ 30, 136, 229, 255 };
    c->candidate_good = (Color){ 67, 160, 71, 255 };
    c->candidate_bad = (Color){ 229, 57, 53, 255 };
    c->human_policy = (Color){ 123, 31, 162, 255 };
    c->candidate_text = (Color){ 0, 0, 0, 255 };
    t.background.kind = BACKGROUND_SOLID;
    t.background.opacity = 1.0f;
    t.background.paper = (PaperParams){ 0.03f, 0.0f, 0.1f, 0, { 139, 90, 43, 255 }, 1 };
    t.board.style = BOARD_STYLE_FLAT;
    t.board.line_width = 1.0f;
    t.board.tint = 0.25f;
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

// Every non-colour theme key, as a typed field (colours use COLOR_FIELDS as
// "colors.<name>").
typedef enum ThemeFieldType { TF_F32, TF_I32, TF_U32, TF_BOOL, TF_STRING, TF_COLOR, TF_ENUM } ThemeFieldType;

typedef struct ThemeField {
    const char *key;
    ThemeFieldType type;
    usize offset;
    const EnumName *names;
    isize name_count;
} ThemeField;

#define FIELD(k, type, member) { k, type, offsetof(Theme, member), NULL, 0 }
#define ENUM(k, member, table) { k, TF_ENUM, offsetof(Theme, member), table, countof(table) }
static const ThemeField THEME_FIELDS[] = {
    FIELD("name", TF_STRING, name),
    FIELD("dark", TF_BOOL, dark),
    ENUM("background.kind", background.kind, BACKGROUND_KINDS),
    ENUM("background.fit", background.fit, BACKGROUND_FITS),
    FIELD("background.image", TF_STRING, background.image),
    FIELD("background.opacity", TF_F32, background.opacity),
    FIELD("background.paper.grain", TF_F32, background.paper.grain),
    FIELD("background.paper.fibers", TF_F32, background.paper.fibers),
    FIELD("background.paper.vignette", TF_F32, background.paper.vignette),
    FIELD("background.paper.stains", TF_I32, background.paper.stains),
    FIELD("background.paper.stain_color", TF_COLOR, background.paper.stain_color),
    FIELD("background.paper.seed", TF_U32, background.paper.seed),
    ENUM("board.style", board.style, BOARD_STYLES),
    FIELD("board.texture", TF_STRING, board.texture),
    FIELD("board.line_width", TF_F32, board.line_width),
    FIELD("board.shadow", TF_BOOL, board.shadow),
    FIELD("board.tint", TF_F32, board.tint),
    ENUM("stones.style", stones.style, STONE_STYLES),
    FIELD("stones.shadow", TF_F32, stones.shadow),
    FIELD("shape.radius", TF_F32, shape.radius),
    FIELD("shape.border_width", TF_F32, shape.border_width),
    FIELD("shape.gap", TF_F32, shape.gap),
    FIELD("shape.padding", TF_F32, shape.padding),
    FIELD("fonts.ui", TF_STRING, fonts.ui),
    FIELD("fonts.ui_bold", TF_STRING, fonts.ui_bold),
    FIELD("fonts.mono", TF_STRING, fonts.mono),
};
#undef FIELD
#undef ENUM

// Applies one key=value. Returns false when the key or value is not understood.
static bool apply_entry(Theme *t, StringView key, StringView value) {
    if (sv_starts_with(key, SV("colors."))) {
        StringView name = sv_drop(key, 7);
        for (i32 i = 0; i < (i32)countof(COLOR_FIELDS); ++i) {
            if (sv_eq(name, sv(COLOR_FIELDS[i].key))) return color_parse(value, (Color *)((u8 *)&t->colors + COLOR_FIELDS[i].offset));
        }
        return false;
    }
    for (i32 i = 0; i < (i32)countof(THEME_FIELDS); ++i) {
        const ThemeField *f = &THEME_FIELDS[i];
        if (!sv_eq(key, sv(f->key))) continue;
        void *p = (u8 *)t + f->offset;
        f64 number = 0;
        switch (f->type) {
            case TF_F32: return sv_parse_f64(value, &number) && (*(f32 *)p = (f32)number, true);
            case TF_I32: return sv_parse_f64(value, &number) && (*(i32 *)p = (i32)number, true);
            case TF_U32: return sv_parse_f64(value, &number) && (*(u32 *)p = (u32)number, true);
            case TF_BOOL: return kv_parse_bool(value, (bool *)p);
            case TF_COLOR: return color_parse(value, (Color *)p);
            case TF_ENUM: return enum_from_text(value, f->names, f->name_count, (i32 *)p);
            case TF_STRING: {
                String *str = p;
                string_free(str);
                *str = string_from(value);
                return true;
            }
        }
    }
    return false;
}

static void push_color(String *out, Color c) {
    if (c.a == 255) string_pushf(out, "#%02X%02X%02X", c.r, c.g, c.b);
    else string_pushf(out, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
}

String theme_write(const Theme *t, StringView comment) {
    String out = { 0 };
    if (comment.len) string_pushf(&out, "# " SV_FMT "\n\n", SV_ARG(comment));
    for (i32 i = 0; i < (i32)countof(THEME_FIELDS); ++i) {
        const ThemeField *f = &THEME_FIELDS[i];
        const void *p = (const u8 *)t + f->offset;
        string_pushf(&out, "%s = ", f->key);
        switch (f->type) {
            case TF_F32: string_pushf(&out, "%g", (f64) * (const f32 *)p); break;
            case TF_I32: string_pushf(&out, "%d", *(const i32 *)p); break;
            case TF_U32: string_pushf(&out, "%u", *(const u32 *)p); break;
            case TF_BOOL: string_push(&out, *(const bool *)p ? SV("true") : SV("false")); break;
            case TF_COLOR: push_color(&out, *(const Color *)p); break;
            case TF_STRING: kv_write_value(&out, string_sv((const String *)p)); break;
            case TF_ENUM: {
                i32 v = *(const i32 *)p;
                for (isize k = 0; k < f->name_count; ++k) {
                    if (f->names[k].value == v) string_push(&out, sv(f->names[k].name));
                }
                break;
            }
        }
        string_push_char(&out, '\n');
    }
    string_push_char(&out, '\n');
    for (i32 i = 0; i < (i32)countof(COLOR_FIELDS); ++i) {
        string_pushf(&out, "colors.%-17s = ", COLOR_FIELDS[i].key);
        push_color(&out, *(const Color *)((const u8 *)&t->colors + COLOR_FIELDS[i].offset));
        string_push_char(&out, '\n');
    }
    return out;
}

void theme_apply_kv(Theme *t, const KvFile *kv, StringView source) {
    vec_foreach(KvEntry, e, &kv->entries) {
        if (sv_eq(e->key, SV("extends"))) continue;
        if (!apply_entry(t, e->key, e->value)) {
            log_warn(SV_FMT ":%d: cannot apply '" SV_FMT " = " SV_FMT "'", SV_ARG(source), e->line, SV_ARG(e->key), SV_ARG(e->value));
        }
    }
}

// ---- Loading ----------------------------------------------------------------------------

typedef struct ThemeSource {
    String id;
    String path;
    KvFile kv;
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
    StringView base_id = kv_get(&src->kv, SV("extends"), SV(""));
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
    theme_apply_kv(&theme, &src->kv, string_sv(&src->path));
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
            if (!sv_ends_with(n, SV(".theme"))) continue;
            String path = path_join(dirs[d], n);
            KvFile kv = { 0 };
            if (!kv_load(&kv, string_sv(&path))) {
                log_warn("theme: cannot read %s", string_cstr(&path));
                string_free(&path);
                continue;
            }
            StringView id = sv_take(n, n.len - 6);
            isize existing = source_find(&sources, id);
            if (existing >= 0) {   // a later directory (user themes) replaces a built-in
                kv_free(&sources.items[existing].kv);
                string_free(&sources.items[existing].path);
                sources.items[existing].kv = kv;
                sources.items[existing].path = path;
            } else {
                vec_push(&sources, (ThemeSource){ .id = string_from(id), .path = path, .kv = kv });
            }
        }
        string_vec_free(&names);
    }
    for (isize i = 0; i < sources.len; ++i) resolve(&sources, i, out);
    vec_foreach(ThemeSource, s, &sources) {
        string_free(&s->id);
        string_free(&s->path);
        kv_free(&s->kv);
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
