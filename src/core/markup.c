#include "core/markup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const SHAPE_PROPS[MARK_SHAPES] = { NULL, "TR", "SQ", "CR", "MA" };

static bool parse_hex_rgb(StringView s, u32 *out) {
    if (s.len != 6) return false;
    u32 v = 0;
    for (isize i = 0; i < 6; ++i) {
        char c = s.data[i];
        u32 d = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (u32)(c - 'A' + 10) : 99;
        if (d > 15) return false;
        v = v << 4 | d;
    }
    *out = v;
    return true;
}

// Calls `fn` for every point of an SGF point value: "aa" or a compressed range "aa:cc".
static void each_point(StringView value, i32 w, i32 h, void (*fn)(Markup *, i32, void *), Markup *m, void *user) {
    isize colon = sv_find_char(value, ':');
    Point a, b;
    if (colon < 0) {
        if (point_from_sgf(value, w, h, &a) && !point_is_pass(a)) fn(m, a.y * w + a.x, user);
        return;
    }
    if (!point_from_sgf(sv_take(value, colon), w, h, &a) || !point_from_sgf(sv_drop(value, colon + 1), w, h, &b)) return;
    if (point_is_pass(a) || point_is_pass(b)) return;
    for (i32 y = MIN(a.y, b.y); y <= MAX(a.y, b.y); ++y) {
        for (i32 x = MIN(a.x, b.x); x <= MAX(a.x, b.x); ++x) fn(m, y * w + x, user);
    }
}

static void set_shape(Markup *m, i32 i, void *user) { m->shape[i] = (u8)(uintptr_t)user; }
static void set_territory(Markup *m, i32 i, void *user) { m->territory[i] = (u8)(uintptr_t)user; }
static void set_shade(Markup *m, i32 i, void *user) {
    m->shaded[i] = true;
    m->shade[i] = *(const u32 *)user;
}

static bool parse_pair(StringView value, i32 w, i32 h, MarkSegment *out) {
    isize colon = sv_find_char(value, ':');
    if (colon < 0) return false;
    if (!point_from_sgf(sv_take(value, colon), w, h, &out->from) || !point_from_sgf(sv_drop(value, colon + 1), w, h, &out->to)) return false;
    return !point_is_pass(out->from) && !point_is_pass(out->to) && !point_eq(out->from, out->to);
}

void markup_read(const GameNode *node, i32 w, i32 h, Markup *m) {
    memset(m, 0, sizeof(*m));
    m->width = w, m->height = h;
    if (!node) return;
    for (const SgfProperty *p = node->props; p; p = p->next) {
        for (i32 v = 0; v < p->value_count; ++v) {
            StringView value = p->values[v];
            for (i32 s = 1; s < MARK_SHAPES; ++s) {
                if (sv_eq(p->ident, sv(SHAPE_PROPS[s]))) each_point(value, w, h, set_shape, m, (void *)(uintptr_t)s);
            }
            if (sv_eq(p->ident, SV("TB"))) each_point(value, w, h, set_territory, m, (void *)(uintptr_t)STONE_BLACK);
            if (sv_eq(p->ident, SV("TW"))) each_point(value, w, h, set_territory, m, (void *)(uintptr_t)STONE_WHITE);
            if (sv_eq(p->ident, SV("LB"))) {
                isize colon = sv_find_char(value, ':');
                Point pt;
                if (colon >= 0 && point_from_sgf(sv_take(value, colon), w, h, &pt) && !point_is_pass(pt)) {
                    StringView text = sv_drop(value, colon + 1);
                    snprintf(m->label[pt.y * w + pt.x].text, MARKUP_MAX_LABEL, SV_FMT, SV_ARG(sv_take(text, MIN(text.len, MARKUP_MAX_LABEL - 1))));
                }
            }
            MarkSegment seg;
            if (sv_eq(p->ident, SV("AR")) && m->arrow_count < MARKUP_MAX_SEGMENTS && parse_pair(value, w, h, &seg)) m->arrows[m->arrow_count++] = seg;
            if (sv_eq(p->ident, SV("LN")) && m->line_count < MARKUP_MAX_SEGMENTS && parse_pair(value, w, h, &seg)) m->lines[m->line_count++] = seg;
            u32 rgb;
            if (sv_eq(p->ident, SV("CGS")) && value.len > 7 && value.data[6] == ':' && parse_hex_rgb(sv_take(value, 6), &rgb)) {
                each_point(sv_drop(value, 7), w, h, set_shade, m, &rgb);
            }
            if (sv_eq(p->ident, SV("CGP")) && value.len > 7 && value.data[6] == ':' && parse_hex_rgb(sv_take(value, 6), &rgb)) {
                PenStroke *s = markup_add_stroke(m, rgb);
                StringView rest = sv_drop(value, 7);
                while (s && rest.len && s->count < MARKUP_MAX_STROKE_POINTS) {
                    StringView pair = sv_trim(sv_chop_char(&rest, ' '));
                    if (!pair.len) continue;
                    isize comma = sv_find_char(pair, ',');
                    f64 x, y;
                    if (comma < 0 || !sv_parse_f64(sv_take(pair, comma), &x) || !sv_parse_f64(sv_drop(pair, comma + 1), &y)) continue;
                    s->points[s->count][0] = (f32)x, s->points[s->count][1] = (f32)y;
                    s->count++;
                }
                if (s && s->count < 2) m->stroke_count--;   // a stroke needs two points
            }
        }
    }
}

void markup_free(Markup *m) {
    free(m->strokes);
    m->strokes = NULL;
    m->stroke_count = 0;
}

bool markup_is_empty(const Markup *m) {
    for (i32 i = 0; i < m->width * m->height; ++i) {
        if (m->shape[i] || m->label[i].text[0] || m->territory[i] || m->shaded[i]) return false;
    }
    return m->arrow_count == 0 && m->line_count == 0 && m->stroke_count == 0;
}

static void add_point_value(GameTree *tree, GameNode *node, const char *ident, i32 w, i32 i, const char *prefix) {
    char coord[3], value[24];
    point_to_sgf((Point){ (i8)(i % w), (i8)(i / w) }, coord);
    snprintf(value, sizeof(value), "%s%s", prefix, coord);
    game_node_add_prop_value(tree, node, sv(ident), sv(value));
}

void markup_write(GameTree *tree, GameNode *node, const Markup *m) {
    static const char *const ALL[] = { "TR", "SQ", "CR", "MA", "LB", "AR", "LN", "TB", "TW", "CGS", "CGP" };
    for (isize i = 0; i < countof(ALL); ++i) {
        if (game_node_find_prop(node, sv(ALL[i]))) game_node_remove_prop(tree, node, sv(ALL[i]));
    }
    i32 w = m->width, n = m->width * m->height;
    for (i32 i = 0; i < n; ++i) {
        if (m->shape[i] > MARK_NONE && m->shape[i] < MARK_SHAPES) add_point_value(tree, node, SHAPE_PROPS[m->shape[i]], w, i, "");
        if (m->territory[i] == STONE_BLACK) add_point_value(tree, node, "TB", w, i, "");
        if (m->territory[i] == STONE_WHITE) add_point_value(tree, node, "TW", w, i, "");
        if (m->label[i].text[0]) {
            char coord[3], value[24];
            point_to_sgf((Point){ (i8)(i % w), (i8)(i / w) }, coord);
            snprintf(value, sizeof(value), "%s:%s", coord, m->label[i].text);
            game_node_add_prop_value(tree, node, SV("LB"), sv(value));
        }
        if (m->shaded[i]) {
            char prefix[8];
            snprintf(prefix, sizeof(prefix), "%06x:", m->shade[i] & 0xFFFFFF);
            add_point_value(tree, node, "CGS", w, i, prefix);
        }
    }
    for (i32 k = 0; k < 2; ++k) {
        const MarkSegment *list = k ? m->lines : m->arrows;
        i32 count = k ? m->line_count : m->arrow_count;
        for (i32 i = 0; i < count; ++i) {
            char a[3], b[3], value[8];
            point_to_sgf(list[i].from, a), point_to_sgf(list[i].to, b);
            snprintf(value, sizeof(value), "%s:%s", a, b);
            game_node_add_prop_value(tree, node, k ? SV("LN") : SV("AR"), sv(value));
        }
    }
    for (i32 s = 0; s < m->stroke_count; ++s) {
        const PenStroke *st = &m->strokes[s];
        String value = string_format("%06x:", st->rgb & 0xFFFFFF);
        for (i32 i = 0; i < st->count; ++i) string_pushf(&value, i ? " %.2f,%.2f" : "%.2f,%.2f", (f64)st->points[i][0], (f64)st->points[i][1]);
        game_node_add_prop_value(tree, node, SV("CGP"), string_sv(&value));
        string_free(&value);
    }
}

bool markup_toggle_segment(MarkSegment *list, i32 *count, Point from, Point to) {
    for (i32 i = 0; i < *count; ++i) {
        bool same = point_eq(list[i].from, from) && point_eq(list[i].to, to);
        bool reversed = point_eq(list[i].from, to) && point_eq(list[i].to, from);
        if (same || reversed) {
            list[i] = list[--*count];
            return false;
        }
    }
    if (*count >= MARKUP_MAX_SEGMENTS || point_eq(from, to)) return false;
    list[(*count)++] = (MarkSegment){ from, to };
    return true;
}

PenStroke *markup_add_stroke(Markup *m, u32 rgb) {
    if (m->stroke_count >= MARKUP_MAX_STROKES) return NULL;
    if (!m->strokes) m->strokes = calloc(MARKUP_MAX_STROKES, sizeof(PenStroke));
    if (!m->strokes) abort();
    PenStroke *s = &m->strokes[m->stroke_count++];
    memset(s, 0, sizeof(*s));
    s->rgb = rgb;
    return s;
}

// Distance from p to the segment a-b (board coordinates).
static f32 segment_distance(f32 px, f32 py, f32 ax, f32 ay, f32 bx, f32 by) {
    f32 dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    f32 t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    f32 ex = ax + t * dx - px, ey = ay + t * dy - py;
    return ex * ex + ey * ey;
}

void markup_erase_at(Markup *m, Point p) {
    i32 i = p.y * m->width + p.x;
    m->shape[i] = MARK_NONE, m->label[i].text[0] = '\0', m->territory[i] = STONE_EMPTY, m->shaded[i] = false;
    for (i32 k = 0; k < 2; ++k) {
        MarkSegment *list = k ? m->lines : m->arrows;
        i32 *count = k ? &m->line_count : &m->arrow_count;
        for (i32 s = *count - 1; s >= 0; --s) {
            if (point_eq(list[s].from, p) || point_eq(list[s].to, p)) list[s] = list[--*count];
        }
    }
    for (i32 s = m->stroke_count - 1; s >= 0; --s) {   // strokes passing within half a line of p
        const PenStroke *st = &m->strokes[s];
        bool near = false;
        for (i32 k = 0; k + 1 < st->count && !near; ++k) {
            near = segment_distance(p.x, p.y, st->points[k][0], st->points[k][1], st->points[k + 1][0], st->points[k + 1][1]) < 0.25f;
        }
        if (near) m->strokes[s] = m->strokes[--m->stroke_count];
    }
}

void markup_clear(Markup *m) {
    i32 w = m->width, h = m->height;
    markup_free(m);
    memset(m, 0, sizeof(*m));
    m->width = w, m->height = h;
}

void markup_next_label(const Markup *m, bool numbers, char out[MARKUP_MAX_LABEL]) {
    for (i32 k = 0; k < 1000; ++k) {
        char candidate[MARKUP_MAX_LABEL];
        if (numbers) snprintf(candidate, sizeof(candidate), "%d", k + 1);
        else if (k < 52) snprintf(candidate, sizeof(candidate), "%c", k < 26 ? 'A' + k : 'a' + (k - 26));
        else snprintf(candidate, sizeof(candidate), "%c%d", 'A' + (k - 52) % 26, (k - 52) / 26 + 2);
        bool used = false;
        for (i32 i = 0; i < m->width * m->height && !used; ++i) used = strcmp(m->label[i].text, candidate) == 0;
        if (!used) {
            memcpy(out, candidate, MARKUP_MAX_LABEL);
            return;
        }
    }
    snprintf(out, MARKUP_MAX_LABEL, "?");
}

i32 markup_shade_counts(const Markup *m, const Board *board, bool empty_only, ShadeCount *out, i32 cap) {
    i32 n = 0;
    for (i32 i = 0; i < m->width * m->height; ++i) {
        if (!m->shaded[i]) continue;
        if (empty_only && board && board->cells[i] != STONE_EMPTY) continue;
        i32 k = 0;
        while (k < n && out[k].rgb != m->shade[i]) ++k;
        if (k == n) {
            if (n == cap) continue;
            out[n++] = (ShadeCount){ m->shade[i], 0 };
        }
        out[k].points++;
    }
    for (i32 a = 1; a < n; ++a) {   // most points first (insertion sort; a handful of colours)
        ShadeCount v = out[a];
        i32 b = a;
        while (b > 0 && out[b - 1].points < v.points) out[b] = out[b - 1], --b;
        out[b] = v;
    }
    return n;
}

i32 markup_empty_region(const Board *b, Point p, bool *region) {
    i32 w = b->width, h = b->height, count = 0;
    memset(region, 0, (usize)(w * h) * sizeof(bool));
    if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h || b->cells[p.y * w + p.x] != STONE_EMPTY) return 0;
    i32 *stack = malloc(sizeof(i32) * (usize)(w * h));
    if (!stack) abort();
    i32 top = 0;
    stack[top++] = p.y * w + p.x;
    region[p.y * w + p.x] = true;
    while (top) {
        i32 i = stack[--top], x = i % w, y = i / w;
        count++;
        const i32 nb[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
        for (i32 k = 0; k < 4; ++k) {
            i32 nx = nb[k][0], ny = nb[k][1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            i32 j = ny * w + nx;
            if (region[j] || b->cells[j] != STONE_EMPTY) continue;
            region[j] = true;
            stack[top++] = j;
        }
    }
    free(stack);
    return count;
}

// ---- Setup stones -------------------------------------------------------------------------------------

// Removes the point value `coord` from property `ident` (keeps the others).
static bool remove_point_value(GameTree *tree, GameNode *node, StringView ident, StringView coord) {
    SgfProperty *p = game_node_find_prop(node, ident);
    if (!p) return false;
    bool found = false;
    String kept[BOARD_MAX_POINTS];
    i32 n = 0;
    for (i32 i = 0; i < p->value_count; ++i) {
        if (sv_eq(p->values[i], coord)) found = true;
        else if (n < BOARD_MAX_POINTS) kept[n++] = string_from(p->values[i]);
    }
    if (!found) {
        for (i32 i = 0; i < n; ++i) string_free(&kept[i]);
        return false;
    }
    game_node_remove_prop(tree, node, ident);
    for (i32 i = 0; i < n; ++i) {
        game_node_add_prop_value(tree, node, ident, string_sv(&kept[i]));
        string_free(&kept[i]);
    }
    return true;
}

void setup_toggle_stone(GameTree *tree, GameNode *node, Point p, Stone color) {
    char c[3];
    point_to_sgf(p, c);
    StringView coord = sv(c);
    // What the point holds before this node's setup: the parent's position.
    Stone before = node->parent ? (Stone)board_at(&node->parent->board, p) : STONE_EMPTY;
    Stone now = (Stone)board_at(&node->board, p);
    remove_point_value(tree, node, SV("AB"), coord);
    remove_point_value(tree, node, SV("AW"), coord);
    remove_point_value(tree, node, SV("AE"), coord);
    Stone want = now == color ? STONE_EMPTY : color;
    if (want == before) return;   // the parent already has it that way
    StringView ident = want == STONE_BLACK ? SV("AB") : want == STONE_WHITE ? SV("AW") : SV("AE");
    game_node_add_prop_value(tree, node, ident, coord);
}
