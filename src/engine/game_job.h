// game_job.h - turns a game line into a BatchAnalyzer job and maps the
// per-turn results back onto the tree's nodes.
#pragma once

#include "engine/batch_analyzer.h"

typedef struct GameJob {
    String id;
    PositionSpec *spec;    // setup + moves of the line (heap)
    GameNode **nodes;      // every node from the base position to the end of the line
    i32 *turn_of;          // turn index for each entry of `nodes`
    isize node_count;
    i32 turns;             // spec->move_count + 1
    i32 received;          // results applied so far
    bool *seen;            // per turn
    bool failed;
    String error;
} GameJob;

// Builds the job for root..end (end = last node of the line to analyse).
void game_job_build(GameJob *job, GameTree *tree, GameNode *end, StringView id, StringView rules);
AnalysisJob game_job_request(const GameJob *job, i32 max_visits);
// Applies a result to every node at that turn. Returns true if it belonged to this job.
bool game_job_apply(GameJob *job, const TurnResult *result);
static inline bool game_job_done(const GameJob *job) { return job->failed || job->received >= job->turns; }
void game_job_free(GameJob *job);
