#include "app/i18n.h"

#include "base/kv.h"
#include "platform/os.h"

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

bool i18n_load(I18n *i18n, StringView path) {
    KvFile kv = { 0 };
    if (!kv_load(&kv, path)) return false;
    vec_foreach(KvEntry, e, &kv.entries) i18n_put(i18n, e->key, e->value);
    kv_free(&kv);
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
