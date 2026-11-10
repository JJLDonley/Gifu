// stone_anim.h - stones easing in when placed and fading out when removed.
//
// Pure (no raylib; the caller passes the time): board_view.c feeds it every
// frame's board and draws with the looks it returns. Small changes (a move and
// its captures) animate; big jumps (opening a file, going to the start, another
// board size) snap into place.
#pragma once

#include "core/board.h"

enum { STONE_ANIM_MAX_CHANGES = 12, STONE_ANIM_GHOSTS = 64 };
#define STONE_IN_SECONDS 0.16
#define STONE_OUT_SECONDS 0.24

typedef struct StoneGhost {   // a removed stone fading out
    Point point;
    Stone color;
    f64 born;
} StoneGhost;

typedef struct StoneAnim {
    u8 prev_cells[BOARD_MAX_POINTS];   // the board seen last frame
    i32 prev_width, prev_height;
    f64 born[BOARD_MAX_POINTS];        // when a stone appeared (0 = no animation)
    StoneGhost ghosts[STONE_ANIM_GHOSTS];
    i32 ghost_count;
} StoneAnim;

typedef struct StoneLook {
    f32 scale;   // of the stone radius
    f32 alpha;
} StoneLook;

void stone_anim_update(StoneAnim *anim, const Board *board, f64 now);
StoneLook stone_anim_placed(const StoneAnim *anim, i32 index, f64 now);   // a stone on the board
StoneLook stone_anim_ghost(const StoneGhost *ghost, f64 now);             // a removed one
f32 stone_anim_marker(const StoneAnim *anim, i32 index, f64 now);         // last-move marker grows in after the stone (0..1)
