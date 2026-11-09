// rank_estimator.h - estimates each player's rank with the human SL network.
//
// Replays a game on an engine with a human network. For every move it asks how
// likely that move is for a player of each rank (KataGo: `kata-set-param
// humanSLProfile rank_X` + `kata-raw-human-nn`); per player, the rank with the
// highest total log-likelihood is the estimate.
#pragma once

#include "engine/engine.h"

enum { RANK_COUNT = 12 };
StringView rank_label(i32 index);     // "20k" ... "9d"
StringView rank_profile(i32 index);   // "rank_20k" ...

// ---- Pure scoring (unit-tested) -------------------------------------------------------------
typedef struct RankTally {
    f64 loglik[RANK_COUNT];
    i32 moves;
} RankTally;

// Adds one move: `p[r]` = probability the rank-r human network gives the played move.
void rank_tally_add(RankTally *tally, const f32 p[RANK_COUNT]);
i32 rank_tally_best(const RankTally *tally);   // -1 without moves
// Posterior (uniform prior) of each rank; sums to 1.
void rank_tally_posterior(const RankTally *tally, f32 out[RANK_COUNT]);

// ---- Per move, saved in the SGF ---------------------------------------------------------------
// CGR[p20k,p15k,...,p9d]: the probability each rank's human network gave the
// move played at that node.
#define RANK_SGF_PROP "CGR"
void rank_probs_format(const f32 p[RANK_COUNT], char *buffer, isize capacity);   // ~110 bytes
bool rank_probs_parse(StringView text, f32 out[RANK_COUNT]);

// Game phases by move number, scaled to the board (19x19: 1-50, 51-150, 151-).
enum { RANK_PHASE_OPENING, RANK_PHASE_MIDDLE, RANK_PHASE_ENDGAME, RANK_PHASES };
i32 rank_phase(i32 move_number, i32 board_points);
i32 rank_phase_start(i32 phase, i32 board_points);   // first move number of the phase
StringView rank_phase_name(i32 phase);                // "opening", "middle", "endgame" (i18n keys report.phase_*)

typedef struct RankSummary {
    RankTally total[3];                // indexed by Stone
    RankTally phase[RANK_PHASES][3];
} RankSummary;
void rank_summary_add(RankSummary *s, Stone color, i32 move_number, i32 board_points, const f32 p[RANK_COUNT]);

// ---- Job on a running engine ----------------------------------------------------------------
// Borrows an engine that has a human network (no extra KataGo process: a GPU
// rarely holds more than two). While the job runs the owner must not use the
// engine or poll it; afterwards it must resync its position and restart analysis.
typedef struct RankProgress {
    i32 done, total;    // scored moves
    bool finished, failed;
    String error;
    RankTally tally[3];   // indexed by Stone
    // Per game move (index into the PositionSpec's moves): the probabilities, if scored.
    i32 move_count;
    f32 (*move_p)[RANK_COUNT];
    bool *move_scored;
} RankProgress;

typedef struct RankEstimator RankEstimator;

// `game` is copied. `restore_profile` is put back when the job is destroyed.
RankEstimator *rank_estimator_start(Engine *engine, const PositionSpec *game, StringView restore_profile);
// Polls the engine and sends the next queries (call every frame).
void rank_estimator_update(RankEstimator *job);
const RankProgress *rank_estimator_progress(const RankEstimator *job);
void rank_estimator_destroy(RankEstimator *job);
