#include "app/i18n.h"

#include "base/os.h"

#include <stdlib.h>
#include <string.h>

static void i18n_grow(I18n *i18n) {
    isize new_capacity = i18n->capacity ? i18n->capacity * 2 : 1024;
    I18nEntry *slots = calloc((usize)new_capacity, sizeof(I18nEntry));
    if (!slots) abort();
    for (isize i = 0; i < i18n->capacity; ++i) {
        I18nEntry e = i18n->slots[i];
        if (!e.key.data) continue;
        isize j = (isize)(e.hash & (u64)(new_capacity - 1));
        while (slots[j].key.data) j = (j + 1) & (new_capacity - 1);
        slots[j] = e;
    }
    free(i18n->slots);
    i18n->slots = slots;
    i18n->capacity = new_capacity;
}

static void i18n_put(I18n *i18n, StringView key, StringView value) {
    if ((i18n->count + 1) * 2 > i18n->capacity) i18n_grow(i18n);
    u64 hash = sv_hash(key);
    isize j = (isize)(hash & (u64)(i18n->capacity - 1));
    while (i18n->slots[j].key.data) {
        if (i18n->slots[j].hash == hash && sv_eq(i18n->slots[j].key, key)) {
            i18n->slots[j].value = arena_copy_sv(&i18n->arena, value);
            return;
        }
        j = (j + 1) & (i18n->capacity - 1);
    }
    i18n->slots[j] = (I18nEntry){ arena_copy_sv(&i18n->arena, key), arena_copy_sv(&i18n->arena, value), hash };
    i18n->count++;
}

static i32 hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes .properties escapes (\n, \t, \uXXXX, \\) into UTF-8.
static void unescape(StringView in, String *out) {
    string_clear(out);
    for (isize i = 0; i < in.len; ++i) {
        char c = in.data[i];
        if (c != '\\' || i + 1 >= in.len) {
            string_push_char(out, c);
            continue;
        }
        char n = in.data[++i];
        if (n == 'n') string_push_char(out, '\n');
        else if (n == 't') string_push_char(out, '\t');
        else if (n == 'u' && i + 4 < in.len) {
            u32 cp = 0;
            bool ok = true;
            for (i32 k = 1; k <= 4; ++k) {
                i32 h = hex_value(in.data[i + k]);
                ok &= h >= 0;
                cp = (cp << 4) | (u32)(h < 0 ? 0 : h);
            }
            if (ok) {
                char utf8[4];
                i32 len = utf8_encode(cp, utf8);
                string_push(out, sv_from_parts(utf8, len));
                i += 4;
            }
        } else string_push_char(out, n);
    }
}

bool i18n_load(I18n *i18n, StringView path) {
    String text = { 0 };
    if (!os_read_file(path, &text)) return false;
    StringView rest = string_sv(&text);
    if (sv_starts_with(rest, SV("\xEF\xBB\xBF"))) rest = sv_drop(rest, 3);
    String logical = { 0 }, value = { 0 };
    while (rest.len > 0) {
        // Join continuation lines ending in a single backslash.
        string_clear(&logical);
        for (;;) {
            StringView line = sv_trim_left(sv_chop_line(&rest));
            if (line.len > 0 && line.data[line.len - 1] == '\\' && rest.len > 0) {
                string_push(&logical, sv_take(line, line.len - 1));
                continue;
            }
            string_push(&logical, line);
            break;
        }
        StringView l = string_sv(&logical);
        if (l.len == 0 || l.data[0] == '#' || l.data[0] == '!') continue;
        isize sep = -1;
        for (isize i = 0; i < l.len; ++i) {
            if ((l.data[i] == '=' || l.data[i] == ':') && (i == 0 || l.data[i - 1] != '\\')) {
                sep = i;
                break;
            }
        }
        if (sep < 0) continue;
        unescape(sv_trim_left(sv_drop(l, sep + 1)), &value);
        i18n_put(i18n, sv_trim(sv_take(l, sep)), string_sv(&value));
    }
    string_free(&logical);
    string_free(&value);
    string_free(&text);
    return true;
}

StringView i18n_get(const I18n *i18n, StringView key, StringView fallback) {
    if (!i18n->capacity) return fallback;
    u64 hash = sv_hash(key);
    isize j = (isize)(hash & (u64)(i18n->capacity - 1));
    while (i18n->slots[j].key.data) {
        if (i18n->slots[j].hash == hash && sv_eq(i18n->slots[j].key, key)) return i18n->slots[j].value;
        j = (j + 1) & (i18n->capacity - 1);
    }
    return fallback;
}

void i18n_free(I18n *i18n) {
    free(i18n->slots);
    arena_free(&i18n->arena);
    *i18n = (I18n){ 0 };
}

isize i18n_collect_codepoints(const I18n *i18n, u32 **out) {
    u8 *seen = calloc(0x30000, 1);
    if (!seen) abort();
    isize count = 0;
    for (isize i = 0; i < i18n->capacity; ++i) {
        StringView v = i18n->slots[i].value;
        while (v.len > 0) {
            u32 cp = utf8_next(&v);
            if (cp < 0x30000 && !seen[cp]) seen[cp] = 1, count++;
        }
    }
    u32 *list = malloc(sizeof(u32) * (usize)MAX(count, 1));
    if (!list) abort();
    isize n = 0;
    for (u32 cp = 0; cp < 0x30000; ++cp) {
        if (seen[cp]) list[n++] = cp;
    }
    free(seen);
    *out = list;
    return n;
}
