// vec.h - typed growable arrays.
//
//   typedef Vec(Move) MoveVec;
//   MoveVec moves = {0};
//   vec_push(&moves, move);
//   vec_foreach(Move, m, &moves) { ... }
//   vec_free(&moves);
#pragma once

#include "base/base.h"

#define Vec(T)       \
    struct {         \
        T *items;    \
        isize len;   \
        isize cap;   \
    }

void *vec__grow(void *items, isize *cap, isize min_cap, isize elem_size);
void vec__free(void *items);

#define vec_reserve(v, n) \
    ((v)->items = vec__grow((v)->items, &(v)->cap, (n), (isize)sizeof(*(v)->items)))
#define vec_push(v, ...) \
    (vec_reserve((v), (v)->len + 1), (v)->items[(v)->len++] = (__VA_ARGS__))
#define vec_pop(v) ((v)->items[--(v)->len])
#define vec_last(v) ((v)->items[(v)->len - 1])
#define vec_clear(v) ((v)->len = 0)
#define vec_free(v) (vec__free((v)->items), (v)->items = NULL, (v)->len = 0, (v)->cap = 0)
#define vec_foreach(T, it, v) for (T *it = (v)->items; it && it < (v)->items + (v)->len; ++it)
