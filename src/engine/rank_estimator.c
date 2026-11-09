#include "engine/rank_estimator.h"

#include "base/log.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const LABELS[RANK_COUNT] = { "20k", "15k", "10k", "7k", "5k", "3k", "1k", "1d", "3d", "5d", "7d", "9d" };
static const char *const PROFILES[RANK_COUNT] = { "rank_20k", "rank_15k", "rank_10k", "rank_7k", "rank_5k", "rank_3k",
                                                  "rank_1k",  "rank_1d",  "rank_3d",  "rank_5d", "rank_7d", "rank_9d" };

StringView rank_label(i32 i) { return i >= 0 && i < RANK_COUNT ? sv(LABELS[i]) : SV("?"); }
StringView rank_profile(i32 i) { return i >= 0 && i < RANK_COUNT ? sv(PROFILES[i]) : SV(""); }

// ---- Scoring ----------------------------------------------------------------------------------

// Floor so one surprising move (a misclick, a tesuji) cannot dominate.
static const f64 MIN_PROBABILITY = 1e-3;

void rank_tally_add(RankTally *t, const f32 p[RANK_COUNT]) {
    for (i32 r = 0; r < RANK_COUNT; ++r) t->loglik[r] += log(fmax((f64)p[r], MIN_PROBABILITY));
    t->moves++;
}

i32 rank_tally_best(const RankTally *t) {
    if (t->moves == 0) return -1;
    i32 best = 0;
    for (i32 r = 1; r < RANK_COUNT; ++r) {
        if (t->loglik[r] > t->loglik[best]) best = r;
    }
    return best;
}

void rank_tally_posterior(const RankTally *t, f32 out[RANK_COUNT]) {
    i32 best = rank_tally_best(t);
    if (best < 0) {
        for (i32 r = 0; r < RANK_COUNT; ++r) out[r] = 1.0f / RANK_COUNT;
        return;
    }
    f64 sum = 0, w[RANK_COUNT];
    for (i32 r = 0; r < RANK_COUNT; ++r) sum += w[r] = exp(t->loglik[r] - t->loglik[best]);
    for (i32 r = 0; r < RANK_COUNT; ++r) out[r] = (f32)(w[r] / sum);
}

void rank_probs_format(const f32 p[RANK_COUNT], char *buffer, isize capacity) {
    isize n = 0;
    buffer[0] = '\0';
    for (i32 r = 0; r < RANK_COUNT && n < capacity; ++r) {
        n += snprintf(buffer + n, (size_t)(capacity - n), r ? ",%.3g" : "%.3g", (f64)p[r]);
    }
}

bool rank_probs_parse(StringView text, f32 out[RANK_COUNT]) {
    StringView rest = sv_trim(text);
    for (i32 r = 0; r < RANK_COUNT; ++r) {
        isize comma = sv_find_char(rest, ',');
        StringView tok = comma >= 0 ? sv_take(rest, comma) : rest;
        f64 v = 0;
        if (!sv_parse_f64(sv_trim(tok), &v) || v < 0 || v > 1) return false;
        out[r] = (f32)v;
        if (r < RANK_COUNT - 1) {
            if (comma < 0) return false;
            rest = sv_drop(rest, comma + 1);
        } else if (comma >= 0) {
            return false;
        }
    }
    return true;
}

i32 rank_phase_start(i32 phase, i32 board_points) {
    f64 scale = (f64)(board_points > 0 ? board_points : 361) / 361.0;
    if (phase <= RANK_PHASE_OPENING) return 1;
    if (phase == RANK_PHASE_MIDDLE) return 1 + (i32)lround(50 * scale);
    return 1 + (i32)lround(150 * scale);
}

i32 rank_phase(i32 move_number, i32 board_points) {
    if (move_number >= rank_phase_start(RANK_PHASE_ENDGAME, board_points)) return RANK_PHASE_ENDGAME;
    if (move_number >= rank_phase_start(RANK_PHASE_MIDDLE, board_points)) return RANK_PHASE_MIDDLE;
    return RANK_PHASE_OPENING;
}

StringView rank_phase_name(i32 phase) {
    static const char *const NAMES[RANK_PHASES] = { "opening", "middle", "endgame" };
    return phase >= 0 && phase < RANK_PHASES ? sv(NAMES[phase]) : SV("");
}

void rank_summary_add(RankSummary *s, Stone color, i32 move_number, i32 board_points, const f32 p[RANK_COUNT]) {
    if (color != STONE_BLACK && color != STONE_WHITE) return;
    rank_tally_add(&s->total[color], p);
    rank_tally_add(&s->phase[rank_phase(move_number, board_points)][color], p);
}

// ---- Job --------------------------------------------------------------------------------------

struct RankEstimator {
    Engine *engine;
    PositionSpec *game;
    PositionSpec *prefix;       // position sent to the engine (game moves 0..next_move)
    i32 next_move;              // next move index to look at
    i32 pending;                // human-policy answers outstanding for the current move
    f32 p[RANK_COUNT];
    Move scoring;               // the move being scored
    u64 position_id;
    char restore[40];
    RankProgress progress;
};

static void fail(RankEstimator *j, StringView message) {
    if (j->progress.finished) return;
    j->progress.failed = j->progress.finished = true;
    j->progress.error = string_from(message);
    log_warn("rank estimate: " SV_FMT, SV_ARG(message));
}

// Expected colour of move i: Black first, then alternating (moves out of turn are not scored).
static bool scoreable(const PositionSpec *g, i32 i) {
    Stone expect = i == 0 ? STONE_BLACK : stone_opponent(g->moves[i - 1].color);
    return !point_is_pass(g->moves[i].point) && g->moves[i].color == expect;
}

// Positions the engine before the next scoreable move and asks for every rank's policy.
static void send_next(RankEstimator *j) {
    const PositionSpec *g = j->game;
    while (j->next_move < g->move_count && !scoreable(g, j->next_move)) j->next_move++;
    if (j->next_move >= g->move_count) {
        j->progress.finished = true;
        return;
    }
    i32 i = j->next_move++;
    j->scoring = g->moves[i];
    j->prefix->move_count = i;
    j->prefix->to_play = j->scoring.color;
    j->prefix->id = j->position_id = 0x7A4C000000000000ull + (u64)i;   // private ids, distinct from the app's
    engine_set_position(j->engine, j->prefix);
    for (i32 r = 0; r < RANK_COUNT; ++r) {
        engine_set_human_profile(j->engine, rank_profile(r));
        engine_request_human_policy(j->engine, j->position_id);
    }
    j->pending = RANK_COUNT;
}

RankEstimator *rank_estimator_start(Engine *engine, const PositionSpec *game, StringView restore_profile) {
    RankEstimator *j = calloc(1, sizeof(RankEstimator));
    if (!j) abort();
    j->game = malloc(sizeof(PositionSpec));
    j->prefix = malloc(sizeof(PositionSpec));
    if (!j->game || !j->prefix) abort();
    memcpy(j->game, game, sizeof(PositionSpec));
    memcpy(j->prefix, game, sizeof(PositionSpec));
    j->engine = engine;
    snprintf(j->restore, sizeof(j->restore), SV_FMT, SV_ARG(restore_profile));
    for (i32 i = 0; i < game->move_count; ++i) j->progress.total += scoreable(game, i);
    j->progress.move_count = game->move_count;
    j->progress.move_p = calloc((usize)MAX(game->move_count, 1), sizeof(*j->progress.move_p));
    j->progress.move_scored = calloc((usize)MAX(game->move_count, 1), sizeof(bool));
    if (!j->progress.move_p || !j->progress.move_scored) abort();
    engine_stop_analysis(engine);   // the GPU is ours until the job ends
    send_next(j);
    return j;
}

void rank_estimator_update(RankEstimator *j) {
    if (!j || j->progress.finished) return;
    EngineEvent ev;
    while (!j->progress.finished && engine_poll(j->engine, &ev)) {
        switch (ev.kind) {
            case ENGINE_EVENT_HUMAN_POLICY: {
                if (ev.position_id != j->position_id || j->pending <= 0) break;
                const HumanPolicy *h = ev.human_policy;
                i32 r = RANK_COUNT - j->pending;   // answers arrive in request order
                j->p[r] = h->policy[j->scoring.point.y * h->width + j->scoring.point.x];
                if (--j->pending == 0) {
                    rank_tally_add(&j->progress.tally[j->scoring.color], j->p);
                    i32 index = j->next_move - 1;   // send_next advanced past the scored move
                    memcpy(j->progress.move_p[index], j->p, sizeof(j->p));
                    j->progress.move_scored[index] = true;
                    j->progress.done++;
                    send_next(j);
                }
                break;
            }
            case ENGINE_EVENT_ERROR: fail(j, ev.text); break;
            case ENGINE_EVENT_STATUS:
                if (ev.status == ENGINE_STATUS_FAILED) fail(j, SV("engine exited"));
                break;
            default: break;   // stray analysis output, logs
        }
    }
}

const RankProgress *rank_estimator_progress(const RankEstimator *j) { return &j->progress; }

i32 rank_progress_store(const RankProgress *p, GameTree *tree, const GameNode *end) {
    enum { MAX_MOVES = 1024 };
    const GameNode **nodes = malloc(sizeof(GameNode *) * MAX_MOVES);
    if (!nodes) abort();
    i32 n = position_spec_move_nodes(end, nodes, MAX_MOVES), stored = 0;
    for (i32 i = 0; i < n && i < p->move_count; ++i) {
        if (!p->move_scored[i]) continue;
        char buffer[160];
        rank_probs_format(p->move_p[i], buffer, (isize)sizeof(buffer));
        game_node_set_prop(tree, (GameNode *)nodes[i], SV(RANK_SGF_PROP), sv(buffer));
        stored++;
    }
    free(nodes);
    return stored;
}

void rank_estimator_destroy(RankEstimator *j) {
    if (!j) return;
    if (j->restore[0]) engine_set_human_profile(j->engine, sv(j->restore));
    string_free(&j->progress.error);
    free(j->progress.move_p);
    free(j->progress.move_scored);
    free(j->game);
    free(j->prefix);
    free(j);
}
