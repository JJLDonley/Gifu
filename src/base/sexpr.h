// sexpr.h - a small s-expression reader for tree-shaped data (the UI layout).
//
//   (name positional... :key value :flag (child ...) (child ...))
//
// Positional args come first; after the first :keyword, an atom following a
// keyword is its value and a keyword with no atom after it is a flag.
//
// Atoms: symbols (anything but whitespace, parens, quotes), :keywords,
// "strings" (with \" \\ \n escapes) and numbers. ';' starts a comment.
// All nodes and their text live in the caller's arena (independent of `text`).
#pragma once

#include "base/arena.h"

typedef enum SexprKind {
    SEXPR_LIST,
    SEXPR_SYMBOL,
    SEXPR_KEYWORD,   // text excludes the leading ':'
    SEXPR_STRING,
    SEXPR_NUMBER,
} SexprKind;

typedef struct Sexpr Sexpr;
struct Sexpr {
    SexprKind kind;
    StringView text;   // symbol / keyword / string contents / number literal
    f64 number;
    Sexpr *first;      // children of a list
    Sexpr *next;       // next sibling
    i32 line, column;
};

typedef struct SexprError {
    i32 line, column;
    char message[128];
} SexprError;

// Parses all top-level forms; *out is a synthetic list holding them.
NODISCARD bool sexpr_parse(Arena *arena, StringView text, Sexpr **out, SexprError *error);

// ---- Node helpers for (head :key value ... args ... (children)) ----------------------
StringView sexpr_head(const Sexpr *list);                    // first symbol, or ""
bool sexpr_is_list(const Sexpr *s, const char *head);        // list whose head is `head`
// Value of :key (atom), NULL if absent. A flag (":key" with no atom after it)
// returns a pointer to the keyword node itself.
const Sexpr *sexpr_prop(const Sexpr *list, const char *key);
bool sexpr_flag(const Sexpr *list, const char *key);         // :key present (as flag or with value)
StringView sexpr_prop_text(const Sexpr *list, const char *key, StringView fallback);
f64 sexpr_prop_number(const Sexpr *list, const char *key, f64 fallback);
// Iterates positional atoms (between the head and the first keyword).
const Sexpr *sexpr_next_arg(const Sexpr *list, const Sexpr *prev);
// Iterates child lists.
const Sexpr *sexpr_next_child(const Sexpr *list, const Sexpr *prev);
