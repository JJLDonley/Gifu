// tsumego_frame.h - fills the board around a life-and-death problem.
//
// A tsumego cut from a full game (or a problem on an otherwise empty board)
// gives the engine a meaningless whole-board evaluation: the empty area dwarfs
// the problem. The frame fills everything outside the problem area so the
// result of the fight decides the game:
//   - the attacker gets a living wall right around the problem,
//   - the defender gets a living area beyond it, the same size as the wall,
//   - both have single-point eyes in a regular pattern so neither can be killed;
//   - optionally, either side gets a ko threat (TsumegoKoThreat).
// Sides of the problem that are close to the board edge are extended to it.
#pragma once

#include "core/board.h"

typedef struct BoardRect {
    i8 x0, y0, x1, y1;   // inclusive, x0 <= x1, y0 <= y1
} BoardRect;

typedef struct TsumegoFrameOptions {
    Stone attacker;      // STONE_EMPTY = detect (the colour on the problem's open sides)
    i32 margin;          // empty lines between the problem and the wall (default 2)
    bool has_problem;    // use `problem` instead of the bounding box of all stones
    BoardRect problem;
    bool ko_threat_attacker, ko_threat_defender;   // give that side a ko threat (if there is room)
} TsumegoFrameOptions;

// A ko threat for `side`, built into side's own area: a block of the
// opponent's stones with exactly two liberties. `move` (among side's stones)
// puts the block in atari; `answer` (among the opponent's stones) connects it
// to the opponent's living area. Cutting at `answer` first is suicide, so the
// threat is only a threat: if it is ignored, `answer` captures the block.
typedef struct TsumegoKoThreat {
    bool placed;
    Point move, answer;
    i32 stones;   // size of the block (what ignoring the threat costs)
} TsumegoKoThreat;

typedef struct TsumegoFrame {
    Stone attacker;
    BoardRect area;             // the problem area left untouched (problem + margin)
    i32 count;
    Move changes[BOARD_MAX_POINTS];   // color STONE_EMPTY = remove a stone
    TsumegoKoThreat ko_threat[3];     // indexed by Stone (when asked for and there was room)
} TsumegoFrame;

// Fills `out`. Returns false with a short reason when no frame fits
// (empty board, problem area covering the whole board, too little room).
bool tsumego_frame_build(const Board *board, const TsumegoFrameOptions *options, TsumegoFrame *out, const char **error);
