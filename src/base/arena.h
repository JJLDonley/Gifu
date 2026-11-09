// arena.h - region allocator. Everything allocated from an arena is freed at
// once with arena_free(). Allocations are zero-initialized.
//
// Typical owners: a GameTree (all nodes/props), the per-frame UI arena
// (reset every frame), a parsed JSON document.
#pragma once

#include "base/base.h"
#include "base/str.h"

typedef struct ArenaBlock ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
    isize default_block_size;
} Arena;

void *arena_alloc(Arena *arena, isize size, isize align);
void arena_reset(Arena *arena);   // keeps the first block, drops the rest
void arena_free(Arena *arena);

StringView arena_copy_sv(Arena *arena, StringView s);       // result is NUL-terminated
StringView arena_format(Arena *arena, const char *fmt, ...) PRINTF_LIKE(2, 3);

#define arena_new(arena, T) ((T *)arena_alloc((arena), (isize)sizeof(T), (isize)_Alignof(T)))
#define arena_new_array(arena, T, count) \
    ((T *)arena_alloc((arena), (isize)sizeof(T) * (isize)(count), (isize)_Alignof(T)))
