#include "base/kv.h"

#include "base/os.h"

static i32 hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void kv_write_value(String *out, StringView v) {
    bool quote = v.len && (v.data[0] == ' ' || v.data[v.len - 1] == ' ' || v.data[0] == '"');
    if (quote) string_push_char(out, '"');
    for (isize i = 0; i < v.len; ++i) {
        char c = v.data[i];
        if (c == '\\') string_push(out, SV("\\\\"));
        else if (c == '\n') string_push(out, SV("\\n"));
        else if (c == '\t') string_push(out, SV("\\t"));
        else string_push_char(out, c);
    }
    if (quote) string_push_char(out, '"');
}

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
                string_push(out, sv_from_parts(utf8, utf8_encode(cp, utf8)));
                i += 4;
            } else {
                string_push_char(out, 'u');
            }
        } else {
            string_push_char(out, n);
        }
    }
}

void kv_parse(KvFile *kv, StringView text) {
    if (sv_starts_with(text, SV("\xEF\xBB\xBF"))) text = sv_drop(text, 3);
    String logical = { 0 }, value = { 0 };
    i32 line_no = 0;
    while (text.len > 0) {
        // Join continuation lines ending in a single backslash.
        string_clear(&logical);
        i32 start_line = line_no + 1;
        for (;;) {
            StringView line = sv_trim_left(sv_chop_line(&text));
            line_no++;
            if (line.len > 0 && line.data[line.len - 1] == '\\' && text.len > 0 &&
                (line.len < 2 || line.data[line.len - 2] != '\\')) {
                string_push(&logical, sv_take(line, line.len - 1));
                continue;
            }
            string_push(&logical, line);
            break;
        }
        StringView l = sv_trim(string_sv(&logical));
        if (l.len == 0 || l.data[0] == '#' || l.data[0] == ';' || l.data[0] == '!') continue;
        isize sep = -1;
        for (isize i = 0; i < l.len; ++i) {
            if ((l.data[i] == '=' || l.data[i] == ':') && (i == 0 || l.data[i - 1] != '\\')) {
                sep = i;
                break;
            }
        }
        if (sep <= 0) continue;
        StringView raw = sv_trim(sv_drop(l, sep + 1));
        if (raw.len >= 2 && raw.data[0] == '"' && raw.data[raw.len - 1] == '"') raw = sv_slice(raw, 1, raw.len - 1);
        unescape(raw, &value);
        KvEntry e = { arena_copy_sv(&kv->arena, sv_trim(sv_take(l, sep))), arena_copy_sv(&kv->arena, string_sv(&value)), start_line };
        vec_push(&kv->entries, e);
    }
    string_free(&logical);
    string_free(&value);
}

bool kv_load(KvFile *kv, StringView path) {
    String text = { 0 };
    if (!os_read_file(path, &text)) return false;
    kv_parse(kv, string_sv(&text));
    string_free(&text);
    return true;
}

void kv_free(KvFile *kv) {
    vec_free(&kv->entries);
    arena_free(&kv->arena);
}

StringView kv_get(const KvFile *kv, StringView key, StringView fallback) {
    for (isize i = kv->entries.len - 1; i >= 0; --i) {
        if (sv_eq(kv->entries.items[i].key, key)) return kv->entries.items[i].value;
    }
    return fallback;
}

bool kv_get_f64(const KvFile *kv, StringView key, f64 *out) {
    StringView v = kv_get(kv, key, SV_NULL);
    return v.data && sv_parse_f64(v, out);
}

bool kv_parse_bool(StringView v, bool *out) {
    if (sv_eq_nocase(v, SV("true")) || sv_eq_nocase(v, SV("yes")) || sv_eq_nocase(v, SV("on")) || sv_eq(v, SV("1"))) {
        *out = true;
        return true;
    }
    if (sv_eq_nocase(v, SV("false")) || sv_eq_nocase(v, SV("no")) || sv_eq_nocase(v, SV("off")) || sv_eq(v, SV("0"))) {
        *out = false;
        return true;
    }
    return false;
}
