// go.h - vocabulary types for the game of Go.
#pragma once

#include "base/base.h"
#include "base/str.h"

enum {
    BOARD_MAX_SIZE = 25,
    BOARD_MAX_POINTS = BOARD_MAX_SIZE * BOARD_MAX_SIZE,
};

typedef enum Stone {
    STONE_EMPTY = 0,
    STONE_BLACK = 1,
    STONE_WHITE = 2,
} Stone;

static inline Stone stone_opponent(Stone s) {
    return s == STONE_BLACK ? STONE_WHITE : s == STONE_WHITE ? STONE_BLACK : STONE_EMPTY;
}

// (0,0) is the top-left corner (SGF orientation). A pass is (-1,-1).
typedef struct Point {
    i8 x, y;
} Point;

#define POINT_PASS ((Point){ -1, -1 })

static inline bool point_is_pass(Point p) { return p.x < 0 || p.y < 0; }
static inline bool point_eq(Point a, Point b) { return a.x == b.x && a.y == b.y; }

typedef struct Move {
    Stone color;
    Point point;
} Move;

// ---- Coordinate conversions (coord.c) ---------------------------------------
// GTP: column letters A..Z skipping I, row 1 at the bottom. "pass" for pass.
NODISCARD bool point_from_gtp(StringView text, i32 width, i32 height, Point *out);
// Writes NUL-terminated text, returns its length. buffer needs 8 bytes.
i32 point_to_gtp(Point p, i32 width, i32 height, char *buffer);
// SGF: two letters 'a'.. from the top-left. Empty or "tt" (<=19) is pass.
NODISCARD bool point_from_sgf(StringView text, i32 width, i32 height, Point *out);
i32 point_to_sgf(Point p, char *buffer);   // buffer needs 3 bytes; pass -> ""
