// i18n.h - translated strings from Java-style .properties files
// (assets/l10n/<language>.properties, UTF-8, \uXXXX escapes allowed).
#pragma once

#include "base/arena.h"

typedef struct I18nEntry {
    StringView key;
    StringView value;
    u64 hash;
} I18nEntry;

typedef struct I18n {
    Arena arena;
    I18nEntry *slots;   // open addressing
    isize capacity;
    isize count;
} I18n;

bool i18n_load(I18n *i18n, StringView path);   // merges into existing entries
StringView i18n_get(const I18n *i18n, StringView key, StringView fallback);
void i18n_free(I18n *i18n);
// All codepoints used by values (to pre-bake font atlases). Caller frees.
isize i18n_collect_codepoints(const I18n *i18n, u32 **out);
