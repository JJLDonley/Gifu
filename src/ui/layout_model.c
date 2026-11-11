#include "ui/layout_model.h"

#include <stdio.h>
#include <stdlib.h>

const char *const LAYOUT_PANELS[] = { "game_info", "winrate_summary", "winrate_graph", "candidates", "move_list",
                                      "report",    "compare",         "variation_tree", "comment",   "engine_log" };
const isize LAYOUT_PANEL_COUNT = (isize)(sizeof(LAYOUT_PANELS) / sizeof(LAYOUT_PANELS[0]));

// ---- A light scan of the text: every list's span, head and parent -----------------------------

typedef struct Span {
    isize start, end;          // '(' .. one past ')'
    isize header_end;          // start of the first child list (or end - 1)
    StringView head;
    i32 parent;
} Span;

typedef Vec(Span) SpanVec;

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void scan(StringView t, SpanVec *out) {
    i32 stack[64];
    i32 depth = 0;
    for (isize i = 0; i < t.len; ++i) {
        char c = t.data[i];
        if (c == ';') {
            while (i < t.len && t.data[i] != '\n') ++i;
        } else if (c == '"') {
            for (++i; i < t.len && t.data[i] != '"'; ++i) {
                if (t.data[i] == '\\') ++i;
            }
        } else if (c == '(') {
            isize h = i + 1;
            while (h < t.len && is_space(t.data[h])) ++h;
            isize e = h;
            while (e < t.len && !is_space(t.data[e]) && t.data[e] != '(' && t.data[e] != ')') ++e;
            Span s = { i, -1, -1, sv_from_parts(t.data + h, e - h), depth > 0 ? stack[depth - 1] : -1 };
            if (depth > 0 && out->items[stack[depth - 1]].header_end < 0) out->items[stack[depth - 1]].header_end = i;
            vec_push(out, s);
            if (depth < (i32)(sizeof(stack) / sizeof(stack[0]))) stack[depth++] = (i32)(out->len - 1);
        } else if (c == ')' && depth > 0) {
            Span *s = &out->items[stack[--depth]];
            s->end = i + 1;
            if (s->header_end < 0) s->header_end = i;
        }
    }
}

static StringView slice(StringView t, isize a, isize b) { return sv_from_parts(t.data + a, b - a); }

static i32 child(const SpanVec *spans, i32 parent, const char *head) {
    for (isize i = 0; i < spans->len; ++i) {
        if (spans->items[i].parent == parent && sv_eq(spans->items[i].head, sv(head))) return (i32)i;
    }
    return -1;
}

bool layout_model_read(StringView text, LayoutModel *m, const char **error) {
    *m = (LayoutModel){ 0 };
    SpanVec spans = { 0 };
    scan(text, &spans);
    i32 toolbar = -1, row = -1, column = -1, board = -1, tabs = -1;
    for (isize i = 0; i < spans.len && toolbar < 0; ++i) {
        if (sv_eq(spans.items[i].head, SV("toolbar"))) toolbar = (i32)i;
    }
    for (isize i = 0; i < spans.len && row < 0; ++i) {
        if (!sv_eq(spans.items[i].head, SV("row"))) continue;
        i32 b = child(&spans, (i32)i, "board"), c = child(&spans, (i32)i, "column");
        if (b >= 0 && c >= 0 && child(&spans, c, "tabs") >= 0) row = (i32)i, board = b, column = c, tabs = child(&spans, c, "tabs");
    }
    bool ok = toolbar >= 0 && row >= 0 && spans.items[toolbar].end > 0 && spans.items[row].end > 0;
    if (!ok) {
        if (error) *error = "the layout needs a (toolbar ...) and a (row (board) (column ... (tabs ...)))";
        vec_free(&spans);
        return false;
    }
    // A toolbar with modes: the editor works on the first mode's items (Analysis).
    i32 modes = child(&spans, toolbar, "modes");
    i32 first_mode = modes >= 0 ? child(&spans, modes, "mode") : -1;
    if (first_mode >= 0 && spans.items[first_mode].end > 0) toolbar = first_mode;
    const Span *ts = &spans.items[toolbar], *rs = &spans.items[row], *cs = &spans.items[column], *bs = &spans.items[board], *tb = &spans.items[tabs];
    m->toolbar_start = ts->start, m->toolbar_end = ts->end;
    m->toolbar_header = string_from(sv_trim_right(slice(text, ts->start, ts->header_end)));
    m->row_start = rs->start, m->row_end = rs->end;
    m->sidebar_left = cs->start < bs->start;
    m->row_header = string_from(sv_trim_right(slice(text, rs->start, rs->header_end)));
    m->board_text = string_from(slice(text, bs->start, bs->end));
    m->column_header = string_from(sv_trim_right(slice(text, cs->start, cs->header_end)));
    StringView header = string_sv(&m->column_header);
    isize w = sv_find(header, SV(":width"));
    f64 width = 400;
    if (w >= 0) {
        StringView rest = sv_trim_left(sv_drop(header, w + 6));
        isize n = 0;
        while (n < rest.len && !is_space(rest.data[n]) && rest.data[n] != ')') ++n;
        if (!sv_parse_f64(sv_take(rest, n), &width)) width = 400;
    }
    m->sidebar_width = (i32)width;
    for (isize i = 0; i < spans.len; ++i) {
        const Span *s = &spans.items[i];
        if (s->parent == toolbar) vec_push(&m->toolbar, string_from(slice(text, s->start, s->end)));
        if (s->parent == column && (i32)i != tabs) vec_push(&m->stack, string_from(slice(text, s->start, s->end)));
    }
    // (tabs a b c :grow): names until the first :keyword, the rest kept as props.
    StringView body = slice(text, tb->start + 1, tb->end - 1);
    body = sv_trim_left(sv_drop(sv_trim_left(body), 4));   // past "tabs"
    while (body.len > 0) {
        isize n = 0;
        while (n < body.len && !is_space(body.data[n])) ++n;
        StringView tok = sv_take(body, n);
        if (tok.len && tok.data[0] == ':') {
            m->tabs_props = string_format(" " SV_FMT, SV_ARG(sv_trim(body)));
            break;
        }
        vec_push(&m->tabs, string_from(tok));
        body = sv_trim_left(sv_drop(body, n));
    }
    vec_free(&spans);
    return true;
}

static String column_header_with_width(const LayoutModel *m) {
    StringView h = string_sv(&m->column_header);
    isize w = sv_find(h, SV(":width"));
    if (w < 0) return string_format("(column :width %d" SV_FMT, m->sidebar_width, SV_ARG(sv_drop(h, 7)));
    StringView rest = sv_drop(h, w + 6);
    isize skip = 0;
    while (skip < rest.len && is_space(rest.data[skip])) ++skip;
    while (skip < rest.len && !is_space(rest.data[skip])) ++skip;
    return string_format(SV_FMT ":width %d" SV_FMT, SV_ARG(sv_take(h, w)), m->sidebar_width, SV_ARG(sv_drop(rest, skip)));
}

String layout_model_write(StringView original, const LayoutModel *m) {
    String toolbar = string_from(m->toolbar_header.len ? string_sv(&m->toolbar_header) : SV("(toolbar"));
    const char *indent = sv_starts_with(string_sv(&m->toolbar_header), SV("(mode")) ? "\n        " : "\n    ";
    vec_foreach(String, item, &m->toolbar) string_pushf(&toolbar, "%s" SV_FMT, indent, SV_ARG(string_sv(item)));
    string_push_char(&toolbar, ')');

    String column = column_header_with_width(m);
    vec_foreach(String, item, &m->stack) string_pushf(&column, "\n      " SV_FMT, SV_ARG(string_sv(item)));
    string_push(&column, SV("\n      (tabs"));
    vec_foreach(String, name, &m->tabs) string_pushf(&column, " " SV_FMT, SV_ARG(string_sv(name)));
    string_pushf(&column, SV_FMT "))", SV_ARG(string_sv(&m->tabs_props)));

    String row = string_from(string_sv(&m->row_header));
    StringView board = string_sv(&m->board_text);
    if (m->sidebar_left) string_pushf(&row, "\n    " SV_FMT "\n    " SV_FMT ")", SV_ARG(string_sv(&column)), SV_ARG(board));
    else string_pushf(&row, "\n    " SV_FMT "\n    " SV_FMT ")", SV_ARG(board), SV_ARG(string_sv(&column)));

    // Splice both pieces back, in text order.
    bool toolbar_first = m->toolbar_start < m->row_start;
    isize a0 = toolbar_first ? m->toolbar_start : m->row_start, a1 = toolbar_first ? m->toolbar_end : m->row_end;
    isize b0 = toolbar_first ? m->row_start : m->toolbar_start, b1 = toolbar_first ? m->row_end : m->toolbar_end;
    StringView first = string_sv(toolbar_first ? &toolbar : &row), second = string_sv(toolbar_first ? &row : &toolbar);
    String out = { 0 };
    string_push(&out, slice(original, 0, a0));
    string_push(&out, first);
    string_push(&out, slice(original, a1, b0));
    string_push(&out, second);
    string_push(&out, slice(original, b1, original.len));
    string_free(&toolbar);
    string_free(&column);
    string_free(&row);
    return out;
}

void layout_model_free(LayoutModel *m) {
    string_vec_free(&m->stack);
    string_vec_free(&m->tabs);
    string_vec_free(&m->toolbar);
    string_free(&m->row_header);
    string_free(&m->toolbar_header);
    string_free(&m->board_text);
    string_free(&m->column_header);
    string_free(&m->tabs_props);
}

static StringView second_token(StringView item, const char *head) {
    StringView s = sv_trim(item);
    if (s.len < 2 || s.data[0] != '(') return SV("");
    s = sv_trim_left(sv_drop(s, 1));
    isize n = 0;
    while (n < s.len && !is_space(s.data[n]) && s.data[n] != ')') ++n;
    if (!sv_eq(sv_take(s, n), sv(head))) return SV("");
    s = sv_trim_left(sv_drop(s, n));
    n = 0;
    while (n < s.len && !is_space(s.data[n]) && s.data[n] != ')') ++n;
    return sv_take(s, n);
}

StringView layout_item_panel(StringView item) { return second_token(item, "panel"); }
StringView layout_item_command(StringView item) { return second_token(item, "button"); }

bool layout_model_shows(const LayoutModel *m, StringView panel) {
    vec_foreach(String, item, &m->stack) {
        if (sv_eq(layout_item_panel(string_sv(item)), panel)) return true;
    }
    vec_foreach(String, name, &m->tabs) {
        if (sv_eq(string_sv(name), panel)) return true;
    }
    return false;
}

isize layout_vec_move(StringVec *v, isize index, isize delta) {
    isize to = index + delta;
    if (index < 0 || index >= v->len || to < 0 || to >= v->len) return index;
    String tmp = v->items[index];
    v->items[index] = v->items[to];
    v->items[to] = tmp;
    return to;
}

void layout_vec_remove(StringVec *v, isize index) {
    if (index < 0 || index >= v->len) return;
    string_free(&v->items[index]);
    for (isize i = index; i + 1 < v->len; ++i) v->items[i] = v->items[i + 1];
    v->len--;
}

// Inserts `item` (taken over) at `index` (clamped).
static void vec_insert(StringVec *v, isize index, String item) {
    vec_push(v, item);
    index = index < 0 ? 0 : index > v->len - 1 ? v->len - 1 : index;
    for (isize i = v->len - 1; i > index; --i) v->items[i] = v->items[i - 1];
    v->items[index] = item;
}

bool layout_model_move_panel(LayoutModel *m, LayoutSlot from, LayoutSlot to) {
    StringVec *src = from.tabs ? &m->tabs : &m->stack, *dst = to.tabs ? &m->tabs : &m->stack;
    if (from.index < 0 || from.index >= src->len || to.index < 0 || to.index > dst->len) return false;
    if (src == dst && (to.index == from.index || to.index == from.index + 1)) return false;   // drops onto itself
    StringView item = string_sv(&src->items[from.index]);
    StringView name = from.tabs ? item : layout_item_panel(item);
    if (to.tabs && !from.tabs && name.len == 0) return false;   // separators etc. stay in the stack
    if (from.tabs && !to.tabs && m->tabs.len <= 1) return false;   // keep one tab
    String moved = from.tabs == to.tabs ? string_from(item)
                   : to.tabs            ? string_from(name)
                                        : string_format("(panel " SV_FMT ")", SV_ARG(name));
    layout_vec_remove(src, from.index);
    isize at = src == dst && to.index > from.index ? to.index - 1 : to.index;
    vec_insert(dst, at, moved);
    return true;
}

String layout_item_with_number(StringView item, StringView key, i32 value) {
    String k = string_format(":" SV_FMT, SV_ARG(key));
    StringView body = sv_trim(item);
    isize at = -1;
    for (isize from = 0; from < body.len;) {   // whole-token match of ":key"
        isize f = sv_find(sv_drop(body, from), string_sv(&k));
        if (f < 0) break;
        f += from;
        isize after = f + k.len;
        if (after < body.len && (is_space(body.data[after]) || body.data[after] == ')')) {
            at = f;
            break;
        }
        from = after;
    }
    String out = { 0 };
    if (at >= 0) {
        isize v = at + k.len;
        while (v < body.len && is_space(body.data[v])) ++v;
        while (v < body.len && !is_space(body.data[v]) && body.data[v] != ')') ++v;
        string_push(&out, sv_take(body, at));
        string_pushf(&out, SV_FMT " %d", SV_ARG(string_sv(&k)), value);
        string_push(&out, sv_drop(body, v));
    } else if (body.len && body.data[body.len - 1] == ')') {
        string_push(&out, sv_take(body, body.len - 1));
        string_pushf(&out, " " SV_FMT " %d)", SV_ARG(string_sv(&k)), value);
    } else {
        string_push(&out, body);
    }
    string_free(&k);
    return out;
}
