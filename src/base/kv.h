// kv.h - .env / .properties style key=value files for flat data
// (themes, keymap, translations, settings).
//
//   # comment            ; comment too
//   colors.background = #FFFFFF
//   name = "Coffee Paper"          quotes are optional (keep spaces / '#')
//   label = 50 km             \n \t \\ \uXXXX escapes
//   long = first part \            trailing backslash continues the line
//          second part
//
// Keys may repeat (later entries usually win; the keymap uses every entry).
// Entries keep file order and remember their line for error messages.
#pragma once

#include "base/arena.h"
#include "base/vec.h"

typedef struct KvEntry {
    StringView key;
    StringView value;
    i32 line;
} KvEntry;

typedef struct KvFile {
    Arena arena;
    Vec(KvEntry) entries;
} KvFile;

void kv_parse(KvFile *kv, StringView text);   // appends entries
NODISCARD bool kv_load(KvFile *kv, StringView path);
void kv_free(KvFile *kv);
// Appends `value` escaped so kv_parse reads it back unchanged (\\, \n, \t; quoted
// when it has outer spaces or starts with a quote).
void kv_write_value(String *out, StringView value);

// Last value for `key`, or `fallback`.
StringView kv_get(const KvFile *kv, StringView key, StringView fallback);
bool kv_get_f64(const KvFile *kv, StringView key, f64 *out);
bool kv_parse_bool(StringView value, bool *out);   // true/false/yes/no/on/off/1/0
