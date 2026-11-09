#include "base/vec.h"

#include <stdlib.h>

void *vec__grow(void *items, isize *cap, isize min_cap, isize elem_size) {
    if (min_cap <= *cap) return items;
    isize new_cap = *cap ? *cap * 2 : 8;
    while (new_cap < min_cap) new_cap *= 2;
    void *grown = realloc(items, (usize)(new_cap * elem_size));
    if (!grown) abort();
    *cap = new_cap;
    return grown;
}

void vec__free(void *items) { free(items); }
