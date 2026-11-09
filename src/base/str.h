// str.h - StringView (borrowed, non-owning) and String (owned, growable).
//
// StringView is the default way to pass text around. It is a pointer + length
// pair and is NOT guaranteed to be NUL-terminated.
// String owns heap memory, is always NUL-terminated, and converts to a
// StringView with string_sv().
#pragma once

#include "base/base.h"

typedef struct StringView {
    const char *data;
    isize len;
} StringView;

typedef struct String {
    char *data;
    isize len;
    isize cap;
} String;

// SV("literal") -> StringView without strlen.
#define SV(lit) ((StringView){ "" lit "", (isize)(sizeof(lit) - 1) })
#define SV_NULL ((StringView){ 0 })
#define SV_FMT "%.*s"
#define SV_ARG(s) (int)(s).len, (s).data

// ---- StringView --------------------------------------------------------------
StringView sv(const char *cstr);
StringView sv_from_parts(const char *data, isize len);
StringView sv_slice(StringView s, isize start, isize end);   // [start, end), clamped
StringView sv_drop(StringView s, isize n);
StringView sv_take(StringView s, isize n);
bool sv_is_empty(StringView s);
bool sv_eq(StringView a, StringView b);
bool sv_eq_nocase(StringView a, StringView b);
bool sv_starts_with(StringView s, StringView prefix);
bool sv_ends_with(StringView s, StringView suffix);
isize sv_find_char(StringView s, char c);                     // -1 when absent
isize sv_find_last_char(StringView s, char c);
isize sv_find(StringView haystack, StringView needle);
StringView sv_trim(StringView s);
StringView sv_trim_left(StringView s);
StringView sv_trim_right(StringView s);
// Splits at the first `delim`: returns the head, advances *s past the delimiter.
StringView sv_chop_char(StringView *s, char delim);
// Skips leading whitespace and returns the next whitespace-delimited token.
StringView sv_chop_token(StringView *s);
StringView sv_chop_line(StringView *s);                       // strips trailing '\r'
NODISCARD bool sv_parse_i64(StringView s, i64 *out);
NODISCARD bool sv_parse_f64(StringView s, f64 *out);
u64 sv_hash(StringView s);                                    // FNV-1a

// ---- String ------------------------------------------------------------------
String string_from(StringView s);
String string_format(const char *fmt, ...) PRINTF_LIKE(1, 2);
void string_reserve(String *s, isize cap);
void string_push(String *s, StringView text);
void string_push_char(String *s, char c);
void string_push_cstr(String *s, const char *cstr);
void string_pushf(String *s, const char *fmt, ...) PRINTF_LIKE(2, 3);
void string_clear(String *s);
void string_free(String *s);

static inline StringView string_sv(const String *s) { return (StringView){ s->data ? s->data : "", s->len }; }
static inline const char *string_cstr(const String *s) { return s->data ? s->data : ""; }

// ---- UTF-8 -------------------------------------------------------------------
// Decodes one codepoint from the front of *s and advances it.
// Invalid sequences decode as U+FFFD and consume one byte.
u32 utf8_next(StringView *s);
// Encodes `cp` into out (needs 4 bytes); returns bytes written.
i32 utf8_encode(u32 cp, char out[4]);
