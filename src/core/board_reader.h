// board_reader.h - reads a Go board from a picture (board sync from the screen).
//
// Works on any 2D board rendering with dark grid lines: Gifu, OGS, Fox,
// Tygem, KGS, Sabaki ... The picture is the area the user marked around the
// board (it may include coordinates and margins).
//   1. Grid: per axis, a profile of thin dark lines (darker than the pixels a
//      few pixels away on both sides: stones and their edges do not count),
//      then the spacing and offset of N evenly spaced lines that best match it,
//      with the midpoints between lines and the places just outside the grid
//      required to be line-free (so a double spacing or an off-by-one grid lose).
//   2. Stones: the board colour is the median of the cell centres (a stone never
//      reaches them); around each intersection, the share of pixels much darker
//      / brighter (or greyer) than the board in a disc inside the stone decides
//      (after readboard's BoardOCR, GPL-3, with thresholds relative to the board).
#pragma once

#include "base/image.h"
#include "core/board.h"

typedef struct BoardGrid {
    i32 size;               // lines per side (9, 13 or 19)
    f32 x0, y0;             // first intersection (pixels)
    f32 step_x, step_y;     // distance between lines
    f32 score;              // fit quality (0..1, higher is better)
    bool light_lines;       // light lines on a dark board
    f32 strength;           // share of line pixels along the grid's lines when detected
} BoardGrid;

// size 0 = try 19, 13 and 9 and keep the best. False if no grid fits.
bool board_grid_detect(const RgbaImage *image, i32 size, BoardGrid *out);

typedef struct BoardReading {
    i32 size;
    u8 cells[BOARD_MAX_POINTS];   // Stone per point, row-major from the top-left
    i32 uncertain;                // points close to a decision threshold
} BoardReading;

void board_read_stones(const RgbaImage *image, const BoardGrid *grid, BoardReading *out);

// Share of line pixels along the grid's lines in this picture (compare with
// grid->strength): near 0 when the board is covered, minimized or not drawn yet.
f32 board_grid_strength(const RgbaImage *image, const BoardGrid *grid);
