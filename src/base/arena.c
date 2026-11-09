#include "base/arena.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ArenaBlock {
    ArenaBlock *next;
    isize used;
    isize capacity;
    u8 data[];
};

enum { ARENA_DEFAULT_BLOCK = 64 * 1024 };

static ArenaBlock *arena_block_new(isize capacity) {
    ArenaBlock *block = malloc(sizeof(ArenaBlock) + (usize)capacity);
    if (!block) abort();
    block->next = NULL;
    block->used = 0;
    block->capacity = capacity;
    return block;
}

// Offset >= used such that data + offset is aligned to `align`.
static isize align_offset(const ArenaBlock *block, isize used, isize align) {
    uintptr_t base = (uintptr_t)block->data;
    uintptr_t p = (base + (uintptr_t)used + (uintptr_t)align - 1) & ~((uintptr_t)align - 1);
    return (isize)(p - base);
}

void *arena_alloc(Arena *arena, isize size, isize align) {
    ASSERT(size >= 0 && align > 0 && (align & (align - 1)) == 0);
    ArenaBlock *block = arena->head;
    if (block) {
        isize offset = align_offset(block, block->used, align);
        if (offset + size <= block->capacity) {
            block->used = offset + size;
            void *p = block->data + offset;
            memset(p, 0, (usize)size);
            return p;
        }
    }
    isize block_size = arena->default_block_size ? arena->default_block_size : ARENA_DEFAULT_BLOCK;
    if (size + align > block_size) block_size = size + align;
    ArenaBlock *fresh = arena_block_new(block_size);
    fresh->next = arena->head;
    arena->head = fresh;
    isize offset = align_offset(fresh, 0, align);
    fresh->used = offset + size;
    void *p = fresh->data + offset;
    memset(p, 0, (usize)size);
    return p;
}

void arena_reset(Arena *arena) {
    if (!arena->head) return;
    // Keep the oldest (last) block, free the newer ones.
    ArenaBlock *block = arena->head;
    while (block->next) {
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    block->used = 0;
    arena->head = block;
}

void arena_free(Arena *arena) {
    ArenaBlock *block = arena->head;
    while (block) {
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
}

StringView arena_copy_sv(Arena *arena, StringView s) {
    char *p = arena_alloc(arena, s.len + 1, 1);
    if (s.len) memcpy(p, s.data, (usize)s.len);
    p[s.len] = '\0';
    return (StringView){ p, s.len };
}

StringView arena_format(Arena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) needed = 0;
    char *p = arena_alloc(arena, needed + 1, 1);
    vsnprintf(p, (usize)needed + 1, fmt, args);
    va_end(args);
    return (StringView){ p, needed };
}
