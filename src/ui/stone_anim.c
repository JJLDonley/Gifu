#include "ui/stone_anim.h"

#include "ui/anim.h"

#include <string.h>

void stone_anim_update(StoneAnim *a, const Board *board, f64 now) {
    i32 n = board->width * board->height;
    bool resized = board->width != a->prev_width || board->height != a->prev_height;
    i32 changes = 0;
    if (!resized) {
        for (i32 i = 0; i < n; ++i) changes += board->cells[i] != a->prev_cells[i];
    }
    bool animate = !resized && changes <= STONE_ANIM_MAX_CHANGES;
    if (resized) memset(a->born, 0, sizeof(a->born));
    for (i32 i = 0; i < n; ++i) {
        u8 before = resized ? STONE_EMPTY : a->prev_cells[i], after = board->cells[i];
        if (before == after) continue;
        a->born[i] = animate ? now : 0.0;
        if (animate && before != STONE_EMPTY && a->ghost_count < STONE_ANIM_GHOSTS) {
            a->ghosts[a->ghost_count++] = (StoneGhost){ board_point(board, i), (Stone)before, now };
        }
    }
    if (!animate) a->ghost_count = 0;   // a jump: nothing from the old position lingers
    memcpy(a->prev_cells, board->cells, (usize)n);
    a->prev_width = board->width;
    a->prev_height = board->height;
    i32 kept = 0;   // drop finished ghosts
    for (i32 i = 0; i < a->ghost_count; ++i) {
        if (now - a->ghosts[i].born < STONE_OUT_SECONDS) a->ghosts[kept++] = a->ghosts[i];
    }
    a->ghost_count = kept;
}

static f32 progress(f64 born, f64 now, f64 seconds) { return born > 0 ? (f32)CLAMP((now - born) / seconds, 0.0, 1.0) : 1.0f; }

StoneLook stone_anim_placed(const StoneAnim *a, i32 index, f64 now) {
    f32 t = progress(a->born[index], now, STONE_IN_SECONDS);
    if (t >= 1.0f) return (StoneLook){ 1.0f, 1.0f };
    return (StoneLook){ 0.82f + 0.18f * ease_out_back(t), ease_out_cubic(MIN(t * 1.6f, 1.0f)) };   // slight overshoot
}

StoneLook stone_anim_ghost(const StoneGhost *g, f64 now) {
    f32 t = progress(g->born, now, STONE_OUT_SECONDS), e = ease_out_cubic(t);
    return (StoneLook){ 1.0f - 0.12f * e, 1.0f - e };
}

f32 stone_anim_marker(const StoneAnim *a, i32 index, f64 now) {
    f32 t = progress(a->born[index], now, STONE_IN_SECONDS);
    return ease_out_back(CLAMP(t * 1.2f - 0.2f, 0.0f, 1.0f));
}
