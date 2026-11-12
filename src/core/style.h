// style.h - how a player plays, measured from their moves (not from the engine):
//
//   height:  share of moves on the 4th line or higher (influence) vs. the 1st-3rd
//            (territory). Opening moves count like any other.
//   contact: share of moves touching an opponent stone (fighting vs. calm).
//   tenuki:  share of answers played away from the opponent's last move (more than
//            STYLE_LOCAL_DISTANCE points in either direction): playing elsewhere.
//
// Passes and moves before the first opponent move are left out where they do not apply.
//
// Raw shares mostly reflect the game (half the board is on the 4th line or higher,
// so everyone looks "influential"). Where the position has KataGo's candidates (their
// share of the search), the same measures are also taken for KataGo's choice there
// (the candidates weighted by their shares): the style is the difference, "more
// influential than KataGo would be in these positions".
#pragma once

#include "core/game_tree.h"

enum { STYLE_LOCAL_DISTANCE = 4 };

// Where a move is: a corner (near two edges), a side (near one) or the centre. "Near"
// is within style_zone_band lines of the edge (6 on 19x19, 4 on 13x13, 3 on 9x9).
typedef enum StyleZone { STYLE_CORNER, STYLE_SIDE, STYLE_CENTER, STYLE_ZONES } StyleZone;
StyleZone style_zone(Point p, i32 width, i32 height);
i32 style_zone_band(i32 width, i32 height);
enum { STYLE_HEAT_SIZE = 19 };   // heat maps are kept on a 19x19 grid (smaller boards scale up)

typedef struct StyleCounts {
    i32 moves;          // board moves (not passes)
    i32 high;           // on line 4 or higher
    i32 contact;        // touching an opponent stone
    i32 answers;        // moves played right after an opponent board move
    i32 tenuki;         // ...away from it
    // Compared with KataGo, over the moves whose position has KataGo's candidates:
    i32 base_moves, base_high, base_contact;   // the player's own counts there
    f32 exp_high, exp_contact;                 // KataGo's, as expected counts (shares)
    i32 base_answers, base_tenuki;
    f32 exp_tenuki;
    // Where: zone counts (all moves), and against KataGo on analysed moves.
    i32 zone[STYLE_ZONES];
    i32 base_zone[STYLE_ZONES];
    f32 exp_zone[STYLE_ZONES];
    // Heat map of the player's moves (19x19 grid, other sizes mapped onto it).
    u16 heat[STYLE_HEAT_SIZE * STYLE_HEAT_SIZE];
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
// Against KataGo in the same positions: the player's share and KataGo's (0..1). False
// without analysed positions.
bool style_vs_engine_height(const StyleCounts *s, f32 *player, f32 *engine);
bool style_vs_engine_contact(const StyleCounts *s, f32 *player, f32 *engine);
bool style_vs_engine_tenuki(const StyleCounts *s, f32 *player, f32 *engine);
bool style_vs_engine_zone(const StyleCounts *s, StyleZone zone, f32 *player, f32 *engine);
f32 style_zone_share(const StyleCounts *s, StyleZone zone);   // all moves

// Line of a point: 1 = the edge.
i32 style_line(Point p, i32 width, i32 height);
