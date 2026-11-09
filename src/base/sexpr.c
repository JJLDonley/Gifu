#include "base/sexpr.h"

#include "base/vec.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct Reader {
    StringView text;
    isize pos;
    i32 line, column;
    Arena *arena;
    SexprError *error;
    String scratch;
} Reader;

static bool fail(Reader *r, const char *message) {
    if (r->error) {
        r->error->line = r->line;
        r->error->column = r->column;
        snprintf(r->error->message, sizeof(r->error->message), "%s", message);
    }
    return false;
}

static char peek(const Reader *r) { return r->pos < r->text.len ? r->text.data[r->pos] : '\0'; }

static char advance(Reader *r) {
    char c = r->text.data[r->pos++];
    if (c == '\n') {
        r->line++;
        r->column = 1;
    } else {
        r->column++;
    }
    return c;
}

static void skip_space(Reader *r) {
    while (r->pos < r->text.len) {
        char c = peek(r);
        if (c == ';') {
            while (r->pos < r->text.len && peek(r) != '\n') advance(r);
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(r);
        } else {
            break;
        }
    }
}

static bool is_delimiter(char c) {
    return c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' || c == ')' || c == '"' || c == ';';
}

static Sexpr *new_node(Reader *r, SexprKind kind) {
    Sexpr *s = arena_new(r->arena, Sexpr);
    s->kind = kind;
    s->line = r->line;
    s->column = r->column;
    return s;
}

static bool read_atom(Reader *r, Sexpr **out) {
    Sexpr *s = new_node(r, SEXPR_SYMBOL);
    if (peek(r) == '"') {
        advance(r);
        string_clear(&r->scratch);
        for (;;) {
            if (r->pos >= r->text.len) return fail(r, "unterminated string");
            char c = advance(r);
            if (c == '"') break;
            if (c == '\\' && r->pos < r->text.len) {
                char e = advance(r);
                string_push_char(&r->scratch, e == 'n' ? '\n' : e == 't' ? '\t' : e);
                continue;
            }
            string_push_char(&r->scratch, c);
        }
        s->kind = SEXPR_STRING;
        s->text = arena_copy_sv(r->arena, string_sv(&r->scratch));
        *out = s;
        return true;
    }
    isize start = r->pos;
    while (!is_delimiter(peek(r))) advance(r);
    // Copy into the arena: the tree must outlive the source text.
    StringView token = arena_copy_sv(r->arena, sv_slice(r->text, start, r->pos));
    if (token.len == 0) return fail(r, "unexpected character");
    f64 number;
    if (token.data[0] == ':' && token.len > 1) {
        s->kind = SEXPR_KEYWORD;
        s->text = sv_drop(token, 1);
    } else if (((token.data[0] >= '0' && token.data[0] <= '9') || token.data[0] == '-' || token.data[0] == '.') &&
               sv_parse_f64(token, &number)) {
        s->kind = SEXPR_NUMBER;
        s->number = number;
        s->text = token;
    } else {
        s->text = token;
    }
    *out = s;
    return true;
}

typedef Vec(Sexpr *) SexprStack;

bool sexpr_parse(Arena *arena, StringView text, Sexpr **out, SexprError *error) {
    Reader r = { .text = text, .line = 1, .column = 1, .arena = arena, .error = error };
    Sexpr *root = new_node(&r, SEXPR_LIST);
    SexprStack stack = { 0 };
    Sexpr *tails_root = NULL;   // last child of root
    Vec(Sexpr *) tails = { 0 }; // last child per open list
    vec_push(&stack, root);
    vec_push(&tails, tails_root);
    bool ok = true;
    for (;;) {
        skip_space(&r);
        if (r.pos >= r.text.len) break;
        char c = peek(&r);
        Sexpr *node = NULL;
        if (c == '(') {
            node = new_node(&r, SEXPR_LIST);
            advance(&r);
        } else if (c == ')') {
            if (stack.len <= 1) {
                ok = fail(&r, "unbalanced ')'");
                break;
            }
            advance(&r);
            stack.len--;
            tails.len--;
            continue;
        } else if (!read_atom(&r, &node)) {
            ok = false;
            break;
        }
        // Append to the innermost open list.
        Sexpr *parent = vec_last(&stack);
        Sexpr **tail = &vec_last(&tails);
        if (*tail) (*tail)->next = node;
        else parent->first = node;
        *tail = node;
        if (node->kind == SEXPR_LIST) {
            vec_push(&stack, node);
            vec_push(&tails, (Sexpr *)NULL);
        }
    }
    if (ok && stack.len > 1) {
        Sexpr *open = vec_last(&stack);
        r.line = open->line;
        r.column = open->column;
        ok = fail(&r, "unclosed '(' (missing ')')");
    }
    vec_free(&stack);
    vec_free(&tails);
    string_free(&r.scratch);
    if (ok) *out = root;
    return ok;
}

// ---- Helpers -----------------------------------------------------------------------------------

StringView sexpr_head(const Sexpr *list) {
    if (!list || list->kind != SEXPR_LIST || !list->first || list->first->kind != SEXPR_SYMBOL) return SV("");
    return list->first->text;
}

bool sexpr_is_list(const Sexpr *s, const char *head) { return s && s->kind == SEXPR_LIST && sv_eq(sexpr_head(s), sv(head)); }

static bool is_value(const Sexpr *s) { return s && s->kind != SEXPR_LIST && s->kind != SEXPR_KEYWORD; }

const Sexpr *sexpr_prop(const Sexpr *list, const char *key) {
    if (!list || list->kind != SEXPR_LIST) return NULL;
    for (const Sexpr *s = list->first ? list->first->next : NULL; s; s = s->next) {
        if (s->kind != SEXPR_KEYWORD || !sv_eq(s->text, sv(key))) continue;
        return is_value(s->next) ? s->next : s;
    }
    return NULL;
}

bool sexpr_flag(const Sexpr *list, const char *key) { return sexpr_prop(list, key) != NULL; }

StringView sexpr_prop_text(const Sexpr *list, const char *key, StringView fallback) {
    const Sexpr *v = sexpr_prop(list, key);
    return v && v->kind != SEXPR_KEYWORD ? v->text : fallback;
}

f64 sexpr_prop_number(const Sexpr *list, const char *key, f64 fallback) {
    const Sexpr *v = sexpr_prop(list, key);
    return v && v->kind == SEXPR_NUMBER ? v->number : fallback;
}

const Sexpr *sexpr_next_arg(const Sexpr *list, const Sexpr *prev) {
    if (!list || list->kind != SEXPR_LIST || !list->first) return NULL;
    // Positional args are the atoms between the head and the first keyword.
    const Sexpr *s = prev ? prev->next : list->first->next;
    for (; s; s = s->next) {
        if (s->kind == SEXPR_KEYWORD) return NULL;
        if (s->kind != SEXPR_LIST) return s;
    }
    return NULL;
}

const Sexpr *sexpr_next_child(const Sexpr *list, const Sexpr *prev) {
    if (!list || list->kind != SEXPR_LIST || !list->first) return NULL;
    for (const Sexpr *s = prev ? prev->next : list->first->next; s; s = s->next) {
        if (s->kind == SEXPR_LIST) return s;
    }
    return NULL;
}
