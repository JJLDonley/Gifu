#include "core/sgf.h"

#include "base/log.h"
#include "platform/os.h"
#include "platform/text_encoding.h"
#include "base/vec.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- Parser ----------------------------------------------------------------------

typedef struct SgfParser {
    StringView text;
    isize pos;
    GameTree *tree;
    SgfError *error;
    String scratch;   // value unescaping buffer
} SgfParser;

typedef Vec(GameNode *) NodeStack;

static bool parser_fail(SgfParser *p, const char *message) {
    if (p->error) {
        p->error->offset = p->pos;
        p->error->line = 1;
        for (isize i = 0; i < p->pos && i < p->text.len; ++i) p->error->line += p->text.data[i] == '\n';
        snprintf(p->error->message, sizeof(p->error->message), "%s", message);
    }
    return false;
}

static void skip_space(SgfParser *p) {
    while (p->pos < p->text.len) {
        char c = p->text.data[p->pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        p->pos++;
    }
}

static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }

// Reads one [value], handling escapes and soft line breaks.
static bool parse_value(SgfParser *p, StringView *out) {
    ASSERT(p->text.data[p->pos] == '[');
    p->pos++;
    string_clear(&p->scratch);
    while (p->pos < p->text.len) {
        char c = p->text.data[p->pos++];
        if (c == ']') {
            *out = arena_copy_sv(&p->tree->arena, string_sv(&p->scratch));
            return true;
        }
        if (c == '\\' && p->pos < p->text.len) {
            char next = p->text.data[p->pos++];
            if (next == '\r' && p->pos < p->text.len && p->text.data[p->pos] == '\n') p->pos++;
            if (next == '\n' || next == '\r') continue;   // soft line break
            string_push_char(&p->scratch, next);
            continue;
        }
        string_push_char(&p->scratch, c);
    }
    return parser_fail(p, "unterminated property value");
}

static bool parse_node_props(SgfParser *p, GameNode *node) {
    for (;;) {
        skip_space(p);
        if (p->pos >= p->text.len) return true;
        char c = p->text.data[p->pos];
        if (!is_upper(c) && !is_lower(c)) return true;
        // FF[3] allowed lowercase letters inside identifiers (e.g. "AddBlack"): keep uppercase only.
        char ident_buf[16];
        i32 ident_len = 0;
        while (p->pos < p->text.len && (is_upper(p->text.data[p->pos]) || is_lower(p->text.data[p->pos]))) {
            char ch = p->text.data[p->pos++];
            if (is_upper(ch) && ident_len < (i32)sizeof(ident_buf)) ident_buf[ident_len++] = ch;
        }
        if (ident_len == 0) return parser_fail(p, "property identifier without uppercase letters");
        skip_space(p);
        if (p->pos >= p->text.len || p->text.data[p->pos] != '[') return parser_fail(p, "property without value");
        StringView ident = sv_from_parts(ident_buf, ident_len);
        while (p->pos < p->text.len && p->text.data[p->pos] == '[') {
            StringView value;
            if (!parse_value(p, &value)) return false;
            game_node_add_prop_value(p->tree, node, ident, value);
            skip_space(p);
        }
    }
}

GameTree *sgf_parse(StringView text, SgfError *error) {
    SgfParser p = { .text = text, .error = error };
    if (error) memset(error, 0, sizeof(*error));
    // Skip any junk before the first "(;" (some servers prepend headers).
    isize start = sv_find(text, SV("(;"));
    if (start < 0) {
        parser_fail(&p, "no game found (missing \"(;\")");
        return NULL;
    }
    p.pos = start;
    p.tree = game_tree_new_empty();

    NodeStack stack = { 0 };
    GameNode *cursor = NULL;   // node the next ';' attaches to
    bool root_used = false, ok = true, done = false;

    while (ok && !done) {
        skip_space(&p);
        if (p.pos >= p.text.len) {
            ok = parser_fail(&p, "unexpected end of file (missing ')')");
            break;
        }
        char c = p.text.data[p.pos];
        switch (c) {
            case '(':
                vec_push(&stack, cursor);
                p.pos++;
                break;
            case ')':
                if (stack.len == 0) {
                    ok = parser_fail(&p, "unbalanced ')'");
                    break;
                }
                cursor = vec_pop(&stack);
                p.pos++;
                if (stack.len == 0) done = true;   // end of the first game
                break;
            case ';': {
                p.pos++;
                GameNode *node;
                if (!root_used) {
                    node = p.tree->root;
                    root_used = true;
                } else if (!cursor) {
                    ok = parser_fail(&p, "node outside of a game tree");
                    break;
                } else {
                    node = game_tree_append_child(p.tree, cursor);
                }
                ok = parse_node_props(&p, node);
                cursor = node;
                break;
            }
            default:
                ok = parser_fail(&p, "unexpected character");
                break;
        }
    }
    vec_free(&stack);
    string_free(&p.scratch);
    if (!ok) {
        game_tree_free(p.tree);
        return NULL;
    }
    p.tree->komi_fixed = sgf_fix_komi(p.tree);
    game_tree_rebuild(p.tree, p.tree->root);
    p.tree->current = p.tree->root;
    return p.tree;
}

f64 sgf_normalize_komi(f64 komi) {
    if (fabs(komi) < 100.0) return komi;
    f64 v = komi / 100.0;   // Fox writes komi x100: 375, 650
    // A quarter-point value is the Chinese "zi" komi (per side): 3.75 zi = 7.5 points.
    f64 quarters = v * 4.0;
    if (fabs(quarters - round(quarters)) < 1e-6 && ((i64)round(fabs(quarters)) % 2) == 1) v *= 2.0;
    return v;
}

bool sgf_fix_komi(GameTree *tree) {
    StringView km = game_node_prop(tree->root, SV("KM"));
    f64 komi = 0;
    if (km.len == 0 || !sv_parse_f64(km, &komi)) return false;
    f64 fixed = sgf_normalize_komi(komi);
    if (fixed == komi) return false;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%g", fixed);
    log_info("SGF komi " SV_FMT " read as %s (Fox records komi x100, Chinese counting in zi)", SV_ARG(km), buffer);
    game_node_set_prop(tree, tree->root, SV("KM"), sv(buffer));
    return true;
}

// Finds the raw CA[...] value (ASCII) without a full parse.
static StringView find_charset(StringView bytes) {
    isize at = sv_find(bytes, SV("CA["));
    if (at < 0) return SV_NULL;
    StringView rest = sv_drop(bytes, at + 3);
    isize end = sv_find_char(rest, ']');
    return end < 0 || end > 40 ? SV_NULL : sv_trim(sv_take(rest, end));
}

GameTree *sgf_parse_bytes(StringView bytes, SgfError *error) {
    if (sv_starts_with(bytes, SV("\xEF\xBB\xBF"))) bytes = sv_drop(bytes, 3);   // UTF-8 BOM
    if (utf8_is_valid(bytes)) return sgf_parse(bytes, error);
    StringView charset = find_charset(bytes);
    String converted = { 0 };
    bool ok = text_to_utf8(charset.len ? charset : SV("GB18030"), bytes, &converted) ||
              text_to_utf8(SV("GB18030"), bytes, &converted);
    GameTree *tree = sgf_parse(ok ? string_sv(&converted) : bytes, error);
    if (tree && ok) game_node_set_prop(tree, tree->root, SV("CA"), SV("UTF-8"));
    string_free(&converted);
    return tree;
}

GameTree *sgf_load_file(StringView path, SgfError *error) {
    String bytes = { 0 };
    if (!os_read_file(path, &bytes)) {
        if (error) snprintf(error->message, sizeof(error->message), "cannot read file");
        return NULL;
    }
    GameTree *tree = sgf_parse_bytes(string_sv(&bytes), error);
    string_free(&bytes);
    return tree;
}

// ---- Writer ----------------------------------------------------------------------

static void write_value(String *out, StringView value) {
    string_push_char(out, '[');
    for (isize i = 0; i < value.len; ++i) {
        char c = value.data[i];
        if (c == ']' || c == '\\') string_push_char(out, '\\');
        string_push_char(out, c);
    }
    string_push_char(out, ']');
}

static void write_node(String *out, const GameNode *node) {
    string_push_char(out, ';');
    for (const SgfProperty *p = node->props; p; p = p->next) {
        string_push(out, p->ident);
        for (i32 i = 0; i < p->value_count; ++i) write_value(out, p->values[i]);
    }
}

typedef struct WriteFrame {
    const GameNode *node;
    bool close_paren;
} WriteFrame;
typedef Vec(WriteFrame) WriteStack;

String sgf_write(const GameTree *tree) {
    String out = { 0 };
    WriteStack stack = { 0 };
    string_push_char(&out, '(');
    vec_push(&stack, (WriteFrame){ tree->root, false });
    i32 nodes_on_line = 0;
    while (stack.len > 0) {
        WriteFrame frame = vec_pop(&stack);
        if (!frame.node) {
            string_push_char(&out, ')');
            continue;
        }
        if (frame.close_paren) string_push_char(&out, '(');
        // Write a straight sequence until a branch point.
        const GameNode *node = frame.node;
        for (;;) {
            write_node(&out, node);
            if (!node->parent || ++nodes_on_line >= 12) {
                string_push_char(&out, '\n');
                nodes_on_line = 0;
            }
            i32 children = game_node_child_count(node);
            if (children == 1) {
                node = node->first_child;
                continue;
            }
            if (children > 1) {
                // Push branches in reverse so the first child is written first.
                const GameNode *kids[BOARD_MAX_POINTS + 1];
                i32 n = 0;
                for (const GameNode *c = node->first_child; c && n < (i32)countof(kids); c = c->next_sibling) {
                    kids[n++] = c;
                }
                for (i32 k = n - 1; k >= 0; --k) {
                    vec_push(&stack, (WriteFrame){ NULL, false });   // closing ')'
                    vec_push(&stack, (WriteFrame){ kids[k], true });
                }
            }
            break;
        }
    }
    string_push(&out, SV(")\n"));
    vec_free(&stack);
    return out;
}

bool sgf_save_file(const GameTree *tree, StringView path) {
    String text = sgf_write(tree);
    bool ok = os_write_file(path, string_sv(&text));
    string_free(&text);
    return ok;
}
