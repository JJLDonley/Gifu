#include "base/str.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- StringView --------------------------------------------------------------

StringView sv(const char *cstr) {
    return (StringView){ cstr ? cstr : "", cstr ? (isize)strlen(cstr) : 0 };
}

StringView sv_from_parts(const char *data, isize len) {
    return (StringView){ data, len };
}

StringView sv_slice(StringView s, isize start, isize end) {
    start = CLAMP(start, 0, s.len);
    end = CLAMP(end, start, s.len);
    return (StringView){ s.data + start, end - start };
}

StringView sv_drop(StringView s, isize n) { return sv_slice(s, n, s.len); }
StringView sv_take(StringView s, isize n) { return sv_slice(s, 0, n); }
bool sv_is_empty(StringView s) { return s.len == 0; }

bool sv_eq(StringView a, StringView b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, (usize)a.len) == 0);
}

bool sv_eq_nocase(StringView a, StringView b) {
    if (a.len != b.len) return false;
    for (isize i = 0; i < a.len; ++i) {
        if (tolower((u8)a.data[i]) != tolower((u8)b.data[i])) return false;
    }
    return true;
}

bool sv_starts_with(StringView s, StringView prefix) {
    return s.len >= prefix.len && sv_eq(sv_take(s, prefix.len), prefix);
}

bool sv_ends_with(StringView s, StringView suffix) {
    return s.len >= suffix.len && sv_eq(sv_drop(s, s.len - suffix.len), suffix);
}

isize sv_find_char(StringView s, char c) {
    for (isize i = 0; i < s.len; ++i) {
        if (s.data[i] == c) return i;
    }
    return -1;
}

isize sv_find_last_char(StringView s, char c) {
    for (isize i = s.len - 1; i >= 0; --i) {
        if (s.data[i] == c) return i;
    }
    return -1;
}

isize sv_find(StringView haystack, StringView needle) {
    if (needle.len == 0) return 0;
    for (isize i = 0; i + needle.len <= haystack.len; ++i) {
        if (memcmp(haystack.data + i, needle.data, (usize)needle.len) == 0) return i;
    }
    return -1;
}

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

StringView sv_trim_left(StringView s) {
    isize i = 0;
    while (i < s.len && is_space(s.data[i])) ++i;
    return sv_drop(s, i);
}

StringView sv_trim_right(StringView s) {
    isize n = s.len;
    while (n > 0 && is_space(s.data[n - 1])) --n;
    return sv_take(s, n);
}

StringView sv_trim(StringView s) { return sv_trim_right(sv_trim_left(s)); }

StringView sv_chop_char(StringView *s, char delim) {
    isize i = sv_find_char(*s, delim);
    if (i < 0) {
        StringView head = *s;
        *s = (StringView){ s->data + s->len, 0 };
        return head;
    }
    StringView head = sv_take(*s, i);
    *s = sv_drop(*s, i + 1);
    return head;
}

StringView sv_chop_token(StringView *s) {
    *s = sv_trim_left(*s);
    isize i = 0;
    while (i < s->len && !is_space(s->data[i])) ++i;
    StringView token = sv_take(*s, i);
    *s = sv_drop(*s, i);
    return token;
}

StringView sv_chop_line(StringView *s) {
    StringView line = sv_chop_char(s, '\n');
    if (line.len > 0 && line.data[line.len - 1] == '\r') line.len--;
    return line;
}

bool sv_parse_i64(StringView s, i64 *out) {
    s = sv_trim(s);
    if (s.len == 0) return false;
    bool negative = false;
    isize i = 0;
    if (s.data[0] == '-' || s.data[0] == '+') {
        negative = s.data[0] == '-';
        i = 1;
        if (s.len == 1) return false;
    }
    i64 value = 0;
    for (; i < s.len; ++i) {
        char c = s.data[i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    *out = negative ? -value : value;
    return true;
}

bool sv_parse_f64(StringView s, f64 *out) {
    s = sv_trim(s);
    char buffer[64];
    if (s.len == 0 || s.len >= (isize)sizeof(buffer)) return false;
    memcpy(buffer, s.data, (usize)s.len);
    buffer[s.len] = '\0';
    char *end = NULL;
    f64 value = strtod(buffer, &end);
    if (end != buffer + s.len) return false;
    *out = value;
    return true;
}

u64 sv_hash(StringView s) {
    u64 h = 1469598103934665603ull;
    for (isize i = 0; i < s.len; ++i) {
        h ^= (u8)s.data[i];
        h *= 1099511628211ull;
    }
    return h;
}

// ---- String ------------------------------------------------------------------

void string_reserve(String *s, isize cap) {
    if (cap + 1 <= s->cap) return;
    isize new_cap = s->cap ? s->cap * 2 : 32;
    while (new_cap < cap + 1) new_cap *= 2;
    char *data = realloc(s->data, (usize)new_cap);
    if (!data) abort();
    s->data = data;
    s->cap = new_cap;
}

String string_from(StringView text) {
    String s = { 0 };
    string_push(&s, text);
    return s;
}

void string_push(String *s, StringView text) {
    string_reserve(s, s->len + text.len);
    if (text.len) memcpy(s->data + s->len, text.data, (usize)text.len);
    s->len += text.len;
    s->data[s->len] = '\0';
}

void string_push_char(String *s, char c) {
    string_reserve(s, s->len + 1);
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

void string_push_cstr(String *s, const char *cstr) { string_push(s, sv(cstr)); }

static void string_vpushf(String *s, const char *fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed <= 0) return;
    string_reserve(s, s->len + needed);
    vsnprintf(s->data + s->len, (usize)needed + 1, fmt, args);
    s->len += needed;
}

void string_pushf(String *s, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    string_vpushf(s, fmt, args);
    va_end(args);
}

String string_format(const char *fmt, ...) {
    String s = { 0 };
    va_list args;
    va_start(args, fmt);
    string_vpushf(&s, fmt, args);
    va_end(args);
    return s;
}

void string_clear(String *s) {
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

void string_free(String *s) {
    free(s->data);
    *s = (String){ 0 };
}

// ---- UTF-8 -------------------------------------------------------------------

u32 utf8_next(StringView *s) {
    if (s->len == 0) return 0;
    const u8 *p = (const u8 *)s->data;
    u32 cp = 0xFFFD;
    isize n = 1;
    if (p[0] < 0x80) {
        cp = p[0];
    } else if ((p[0] & 0xE0) == 0xC0 && s->len >= 2 && (p[1] & 0xC0) == 0x80) {
        cp = ((u32)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && s->len >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((u32)(p[0] & 0x0F) << 12) | ((u32)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        n = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && s->len >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
               (p[3] & 0xC0) == 0x80) {
        cp = ((u32)(p[0] & 0x07) << 18) | ((u32)(p[1] & 0x3F) << 12) | ((u32)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        n = 4;
    }
    *s = sv_drop(*s, n);
    return cp;
}

i32 utf8_encode(u32 cp, char out[4]) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
