// rank_estimator.h - estimates each player's rank with the human SL network.
//
// Replays a game in its own KataGo process (so live analysis is untouched).
// For every move it asks the human network how likely that move is for a
// player of each rank (`kata-set-param humanSLProfile rank_X` +
// `kata-raw-human-nn`); per player, the rank with the highest total
// log-likelihood is the estimate.
#pragma once

#include "engine/engine.h"
#include "engine/katago_engine.h"

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

// ---- Background job -------------------------------------------------------------------------
typedef struct RankProgress {
    i32 done, total;    // scored moves
    bool finished, failed;
    String error;
    RankTally tally[3];   // indexed by Stone
} RankProgress;

typedef struct RankEstimator RankEstimator;

// `settings` must include a human_model. `game` is copied.
RankEstimator *rank_estimator_start(const KataGoSettings *settings, const PositionSpec *game, String *error);
// Pumps the engine (call every frame); cheap when idle.
void rank_estimator_update(RankEstimator *job);
const RankProgress *rank_estimator_progress(const RankEstimator *job);
void rank_estimator_destroy(RankEstimator *job);
