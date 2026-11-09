#include "engine/rank_estimator.h"

#include "base/log.h"
#include "engine/gtp_client.h"

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

// ---- Job --------------------------------------------------------------------------------------

struct RankEstimator {
    GtpClient *gtp;
    PositionSpec *game;
    i32 next_move;              // next move index to send
    i32 pending;                // raw-nn answers outstanding for the current move
    i32 raw_id[RANK_COUNT];
    f32 p[RANK_COUNT];
    Move scoring;               // the move being scored
    Stone expected;             // KataGo's next player (moves of the other colour are not scored)
    HumanPolicy *policy;        // parse scratch
    RankProgress progress;
};

static i32 send(RankEstimator *j, const char *fmt, ...) PRINTF_LIKE(2, 3);
static i32 send(RankEstimator *j, const char *fmt, ...) {
    char buffer[8192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return gtp_client_send(j->gtp, sv(buffer));
}

static const char *color_name(Stone c) { return c == STONE_WHITE ? "W" : "B"; }

static void fail(RankEstimator *j, StringView message) {
    if (j->progress.finished) return;
    j->progress.failed = j->progress.finished = true;
    j->progress.error = string_from(message);
    log_warn("rank estimate: " SV_FMT, SV_ARG(message));
}

static bool scoreable(const RankEstimator *j, Move m) { return !point_is_pass(m.point) && m.color == j->expected; }

// Sends the queries for the next scoreable move (playing the others through).
static void send_next(RankEstimator *j) {
    const PositionSpec *g = j->game;
    while (j->next_move < g->move_count) {
        Move m = g->moves[j->next_move++];
        char coord[8];
        point_to_gtp(m.point, g->width, g->height, coord);
        if (scoreable(j, m)) {
            for (i32 r = 0; r < RANK_COUNT; ++r) {
                send(j, "kata-set-param humanSLProfile %s", PROFILES[r]);
                j->raw_id[r] = send(j, "kata-raw-human-nn 0");
            }
            j->pending = RANK_COUNT;
            j->scoring = m;
        }
        send(j, "play %s %s", color_name(m.color), coord);
        j->expected = stone_opponent(m.color);
        if (j->pending) return;
    }
    j->progress.finished = true;
}

RankEstimator *rank_estimator_start(const KataGoSettings *s, const PositionSpec *game, String *error) {
    if (!s->human_model.len) {
        if (error) *error = string_from(SV("rank estimation needs an engine profile with a human_model"));
        return NULL;
    }
    StringView args[24];
    isize argc = 0;
    args[argc++] = SV("gtp");
    args[argc++] = SV("-model");
    args[argc++] = s->model;
    args[argc++] = SV("-human-model");
    args[argc++] = s->human_model;
    if (s->config.len) {
        args[argc++] = SV("-config");
        args[argc++] = s->config;
    }
    ProcessSpec spec = { s->program, args, argc, s->working_dir, s->launcher, s->launcher_count };
    GtpClient *gtp = gtp_client_start(&spec, error);
    if (!gtp) return NULL;

    RankEstimator *j = calloc(1, sizeof(RankEstimator));
    if (!j) abort();
    j->game = malloc(sizeof(PositionSpec));
    j->policy = malloc(sizeof(HumanPolicy));
    if (!j->game || !j->policy) abort();
    memcpy(j->game, game, sizeof(PositionSpec));
    j->gtp = gtp;
    j->expected = STONE_BLACK;
    for (i32 i = 0; i < game->move_count; ++i) {
        Stone expect = i == 0 ? STONE_BLACK : stone_opponent(game->moves[i - 1].color);
        if (!point_is_pass(game->moves[i].point) && game->moves[i].color == expect) j->progress.total++;
    }
    // Same position setup as the interactive engine.
    if (game->width == game->height) send(j, "boardsize %d", game->width);
    else send(j, "rectangular_boardsize %d %d", game->width, game->height);
    send(j, "clear_board");
    send(j, "komi %g", game->komi);
    if (game->rules.len) send(j, "kata-set-rules " SV_FMT, SV_ARG(game->rules));
    if (game->setup_count > 0) {
        String cmd = string_from(SV("set_position"));
        for (i32 i = 0; i < game->setup_count; ++i) {
            char coord[8];
            point_to_gtp(game->setup[i].point, game->width, game->height, coord);
            string_pushf(&cmd, " %s %s", color_name(game->setup[i].color), coord);
        }
        gtp_client_send(gtp, string_sv(&cmd));
        string_free(&cmd);
    }
    send_next(j);
    return j;
}

void rank_estimator_update(RankEstimator *j) {
    if (!j || !j->gtp) return;
    GtpEvent ev;
    while (gtp_client_poll(j->gtp, &ev)) {
        if (ev.kind == GTP_EVENT_EXITED) fail(j, SV("engine exited"));
        if (ev.kind == GTP_EVENT_RESPONSE && j->pending > 0) {
            for (i32 r = 0; r < RANK_COUNT; ++r) {
                if (ev.id != j->raw_id[r]) continue;
                const PositionSpec *g = j->game;
                if (!ev.success || !kata_raw_nn_parse(string_sv(&ev.text), g->width, g->height, j->policy)) {
                    fail(j, ev.success ? SV("unexpected kata-raw-human-nn output") : sv_trim(string_sv(&ev.text)));
                    break;
                }
                j->p[r] = j->policy->policy[j->scoring.point.y * g->width + j->scoring.point.x];
                if (--j->pending == 0) {
                    rank_tally_add(&j->progress.tally[j->scoring.color], j->p);
                    j->progress.done++;
                    send_next(j);
                }
            }
        } else if (ev.kind == GTP_EVENT_RESPONSE && !ev.success && !j->progress.finished) {
            fail(j, sv_trim(string_sv(&ev.text)));   // e.g. unknown humanSLProfile, bad position
        }
        gtp_event_free(&ev);
    }
}

const RankProgress *rank_estimator_progress(const RankEstimator *j) { return &j->progress; }

void rank_estimator_destroy(RankEstimator *j) {
    if (!j) return;
    gtp_client_stop(j->gtp);
    string_free(&j->progress.error);
    free(j->game);
    free(j->policy);
    free(j);
}
