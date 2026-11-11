// style.h - how a player plays, measured from their moves (not from the engine):
//
//   height:  share of moves on the 4th line or higher (influence) vs. the 1st-3rd
//            (territory). Opening moves count like any other.
//   contact: share of moves touching an opponent stone (fighting vs. calm).
//   tenuki:  share of answers played away from the opponent's last move (more than
//            STYLE_LOCAL_DISTANCE points in either direction): playing elsewhere.
//
// Passes and moves before the first opponent move are left out where they do not apply.
#pragma once

#include "core/game_tree.h"

enum { STYLE_LOCAL_DISTANCE = 4 };

typedef struct StyleCounts {
    i32 moves;          // board moves (not passes)
    i32 high;           // on line 4 or higher
    i32 contact;        // touching an opponent stone
    i32 answers;        // moves played right after an opponent board move
    i32 tenuki;         // ...away from it
} StyleCounts;

typedef struct StyleReport {
    StyleCounts player[3];   // indexed by Stone
} StyleReport;

// Counts over `line` (root..end): each move against the position before it.
void style_compute(GameNode *const *line, isize count, StyleReport *out);
void style_add(StyleCounts *to, const StyleCounts *from);

// Shares 0..1 (0 when there is nothing to count).
f32 style_height(const StyleCounts *s);    // 0 = all low (territory) .. 1 = all high (influence)
f32 style_contact(const StyleCounts *s);
f32 style_tenuki(const StyleCounts *s);

// Line of a point: 1 = the edge.
i32 style_line(Point p, i32 width, i32 height);
