// game_report.h - "Hawk Eye" style summary of an analysed game line.
#pragma once

#include "core/game_tree.h"

enum { REPORT_WORST = 10 };

// Game phases by move number, scaled to the board (19x19: 1-50, 51-150, 151-).
enum { GAME_PHASE_OPENING, GAME_PHASE_MIDDLE, GAME_PHASE_ENDGAME, GAME_PHASES };
i32 game_phase(i32 move_number, i32 board_points);
i32 game_phase_start(i32 phase, i32 board_points);   // first move number of the phase

// Accuracy of one move, 0..100, from the points it lost: 100 * exp(-loss / 8)
// (1 point -> 88, 3 -> 69, 5 -> 54, 10 -> 29). Gains count as 100.
f32 move_accuracy(f32 score_loss);
// Share of the search (0..1) the played move got in the position before it;
// false when the parent has no shares (older analysis). 0 if it was not a candidate.
bool node_played_share(const GameNode *node, f32 *share);

// Loss histogram buckets (upper bounds, increasing; the last bucket is open-ended).
// The user edits them on the Distribution chart. They also define the counts: a
// mistake loses at least winrate[1] (5%), a blunder at least winrate[2] (10%).
enum { LOSS_BUCKETS = 5 };
typedef struct LossLimits {
    f32 score[LOSS_BUCKETS - 1];     // points: 0.5 2 5 10
    f32 winrate[LOSS_BUCKETS - 1];   // 0..1: 0.02 0.05 0.10 0.20
} LossLimits;
const LossLimits *loss_limits(void);
void loss_limits_set(const LossLimits *limits);   // kept increasing
LossLimits loss_limits_default(void);
// "2,5,10,20" (percent for win rate, points for score) <-> limits; parse keeps the
// defaults for anything missing or not increasing.
void loss_limits_parse(StringView winrate_percent, StringView score, LossLimits *out);
String loss_limits_format(const f32 *limits, f32 scale);
bool loss_is_mistake(f32 winrate_loss);   // [winrate[1], winrate[2])
bool loss_is_blunder(f32 winrate_loss);   // >= winrate[2]

typedef struct StageReport {
    i32 moves, top1, top3;
    i32 mistakes, blunders;
    f32 agreement;         // 0..100, mean share of the search on the played move
    i32 agreement_moves;   // moves where the shares are known
    f32 accuracy;          // 0..100, mean of move_accuracy
    f32 avg_winrate_loss;  // 0..1
    f32 avg_score_loss;    // points
} StageReport;

typedef struct PlayerReport {
    i32 moves;            // moves with a known loss
    i32 top1, top3;       // moves matching the engine's best / top three
    i32 mistakes;         // win-rate loss > 5%
    i32 blunders;         // win-rate loss > 10%
    f32 avg_winrate_loss; // 0..1
    f32 avg_score_loss;   // points
    f32 accuracy;         // 0..100, mean of move_accuracy
    // Engine agreement (LizzieYzy's "accuracy"): how much of KataGo's search went
    // to the move that was played, 0..100, over moves where the shares are known.
    f32 agreement;
    i32 agreement_moves;
    StageReport stage[GAME_PHASES];
    i32 score_hist[LOSS_BUCKETS];
    i32 winrate_hist[LOSS_BUCKETS];
    GameNode *worst[REPORT_WORST];   // biggest win-rate losses, worst first
    f32 worst_loss[REPORT_WORST];
    i32 worst_count;
    GameNode *worst_points[REPORT_WORST];   // biggest point losses, worst first
    f32 worst_points_loss[REPORT_WORST];
    i32 worst_points_count;
} PlayerReport;

typedef struct GameReport {
    PlayerReport player[3];   // indexed by Stone
    i32 analysed_nodes;
    i32 total_moves;
} GameReport;

// Computes the report over `line` (root..end, e.g. game_tree_active_line).
void game_report_compute(GameNode *const *line, isize count, GameReport *out);
static inline f32 player_accuracy(const PlayerReport *p) { return p->moves ? (f32)p->top1 / (f32)p->moves : 0.0f; }
