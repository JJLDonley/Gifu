// game_report.h - "Hawk Eye" style summary of an analysed game line.
#pragma once

#include "core/game_tree.h"

enum { REPORT_WORST = 5 };

typedef struct PlayerReport {
    i32 moves;            // moves with a known loss
    i32 top1, top3;       // moves matching the engine's best / top three
    i32 mistakes;         // win-rate loss > 5%
    i32 blunders;         // win-rate loss > 10%
    f32 avg_winrate_loss; // 0..1
    f32 avg_score_loss;   // points
    GameNode *worst[REPORT_WORST];   // biggest win-rate losses, worst first
    f32 worst_loss[REPORT_WORST];
    i32 worst_count;
} PlayerReport;

typedef struct GameReport {
    PlayerReport player[3];   // indexed by Stone
    i32 analysed_nodes;
    i32 total_moves;
} GameReport;

// Computes the report over `line` (root..end, e.g. game_tree_active_line).
void game_report_compute(GameNode *const *line, isize count, GameReport *out);
static inline f32 player_accuracy(const PlayerReport *p) { return p->moves ? (f32)p->top1 / (f32)p->moves : 0.0f; }
