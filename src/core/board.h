// board.h - a Go position and the rules for changing it.
// Pure value type: copying a Board copies the whole position.
#pragma once

#include "core/go.h"

typedef enum MoveError {
    MOVE_OK = 0,
    MOVE_ERR_OUT_OF_BOUNDS,
    MOVE_ERR_OCCUPIED,
    MOVE_ERR_KO,
    MOVE_ERR_SUICIDE,
} MoveError;

typedef struct BoardRules {
    bool allow_suicide;   // tromp-taylor / NZ style multi-stone suicide
    bool ignore_ko;       // used when replaying untrusted SGFs
} BoardRules;

typedef struct Board {
    i32 width, height;
    u8 cells[BOARD_MAX_POINTS];   // Stone, row-major with stride = width
    i32 ko_index;                 // point forbidden by simple ko, or -1
    i32 captures[3];              // indexed by Stone: prisoners taken BY that colour
    u64 hash;                     // zobrist hash of stones only
} Board;

void board_init(Board *board, i32 width, i32 height);

static inline bool board_contains(const Board *b, Point p) {
    return p.x >= 0 && p.y >= 0 && p.x < b->width && p.y < b->height;
}
static inline i32 board_index(const Board *b, Point p) { return p.y * b->width + p.x; }
static inline Point board_point(const Board *b, i32 index) {
    return (Point){ (i8)(index % b->width), (i8)(index / b->width) };
}
static inline Stone board_at(const Board *b, Point p) { return (Stone)b->cells[board_index(b, p)]; }

// Setup placement (AB/AW/AE): no captures, clears ko.
void board_set(Board *board, Point p, Stone stone);

MoveError board_check(const Board *board, Move move, BoardRules rules);
// Plays the move; on error the board is unchanged.
MoveError board_play(Board *board, Move move, BoardRules rules);

i32 board_liberties(const Board *board, Point p);
// Fills `out` with indices of the group containing p; returns its size.
i32 board_group(const Board *board, Point p, i32 *out, i32 capacity);
i32 board_stone_count(const Board *board, Stone stone);
bool board_is_star_point(i32 size, i32 x, i32 y);

const char *move_error_name(MoveError err);
