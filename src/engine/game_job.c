#include "engine/game_job.h"

#include <stdlib.h>
#include <string.h>

static bool has_setup_props(const GameNode *node) {
    return game_node_find_prop(node, SV("AB")) || game_node_find_prop(node, SV("AW")) || game_node_find_prop(node, SV("AE"));
}

void game_job_build(GameJob *job, GameTree *tree, GameNode *end, StringView id, StringView rules) {
    memset(job, 0, sizeof(*job));
    job->tree = tree;
    job->id = string_from(id);
    job->spec = malloc(sizeof(PositionSpec));
    if (!job->spec) abort();
    position_spec_from_node(tree, end, rules, job->spec);

    // root..end path, then keep the part from the base position (the last node
    // with setup stones, as position_spec_from_node does) onwards.
    isize depth = 0;
    for (GameNode *n = end; n; n = n->parent) ++depth;
    GameNode **path = malloc(sizeof(GameNode *) * (usize)depth);
    if (!path) abort();
    isize i = depth;
    for (GameNode *n = end; n; n = n->parent) path[--i] = n;
    isize base = 0;
    for (isize k = 0; k < depth; ++k) {
        if (has_setup_props(path[k])) base = k;
    }
    job->node_count = depth - base;
    job->nodes = malloc(sizeof(GameNode *) * (usize)job->node_count);
    job->turn_of = malloc(sizeof(i32) * (usize)job->node_count);
    if (!job->nodes || !job->turn_of) abort();
    // Turn k = position after k moves; nodes without a move share the turn
    // of the move before them. A move on the base node itself (no setup)
    // is moves[0], so it counts too.
    bool base_move_counted = !has_setup_props(path[base]) && path[base]->has_move;
    i32 turn = base_move_counted ? 1 : 0;
    for (isize k = 0; k < job->node_count; ++k) {
        GameNode *n = path[base + k];
        if (k > 0 && n->has_move) ++turn;
        job->nodes[k] = n;
        job->turn_of[k] = MIN(turn, job->spec->move_count);
    }
    free(path);
    job->turns = job->spec->move_count + 1;
    job->seen = calloc((usize)job->turns, sizeof(bool));
    if (!job->seen) abort();
}

AnalysisJob game_job_request(const GameJob *job, i32 max_visits) {
    const PositionSpec *s = job->spec;
    Stone first = s->move_count > 0 ? s->moves[0].color : s->to_play;
    return (AnalysisJob){
        .id = string_sv(&job->id),
        .width = s->width,
        .height = s->height,
        .komi = s->komi,
        .rules = s->rules,
        .setup = s->setup,
        .setup_count = s->setup_count,
        .moves = s->moves,
        .move_count = s->move_count,
        .first_player = first,
        .max_visits = max_visits,
    };
}

bool game_job_apply(GameJob *job, const TurnResult *r) {
    if (!sv_eq(string_sv(&r->job_id), string_sv(&job->id))) return false;
    if (!r->ok) {
        job->failed = true;
        string_free(&job->error);
        job->error = string_from(string_sv(&r->error));
        return true;
    }
    if (r->turn < 0 || r->turn >= job->turns) return true;
    for (isize k = 0; k < job->node_count; ++k) {
        if (job->turn_of[k] == r->turn) game_node_update_eval(job->tree, job->nodes[k], &r->eval);
    }
    if (!job->seen[r->turn]) {
        job->seen[r->turn] = true;
        job->received++;
    }
    return true;
}

void game_job_free(GameJob *job) {
    string_free(&job->id);
    string_free(&job->error);
    free(job->spec);
    free(job->nodes);
    free(job->turn_of);
    free(job->seen);
    memset(job, 0, sizeof(*job));
}
