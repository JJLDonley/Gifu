#include "engine/katago_engine.h"

#include "base/log.h"
#include "base/vec.h"
#include "engine/gtp_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef Vec(Move) MoveVec;

typedef struct KataGoEngine {
    Engine base;   // must be first
    String name;
    GtpClient *gtp;
    EngineStatus status;
    i32 version_id;

    // What the engine currently has loaded.
    bool synced;
    i32 width, height;
    f32 komi;
    String rules;
    MoveVec setup;
    MoveVec moves;
    u64 position_id;
    Stone to_play;

    // Analysis state.
    bool analyzing;
    AnalysisRequest request;
    i32 analysis_id;   // id of the kata-analyze command whose output is current
    AnalysisSnapshot snapshot;

    // Scoring.
    i32 score_id;
    u64 score_position;

    // Move generation.
    i32 genmove_id;
    MoveRequest genmove;

    GtpEvent last_event;   // owns text referenced by the last EngineEvent
} KataGoEngine;

static KataGoEngine *as_katago(Engine *e) { return (KataGoEngine *)e; }
static const KataGoEngine *as_katago_const(const Engine *e) { return (const KataGoEngine *)e; }

static i32 send(KataGoEngine *k, const char *fmt, ...) PRINTF_LIKE(2, 3);
static i32 send(KataGoEngine *k, const char *fmt, ...) {
    char buffer[8192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return gtp_client_send(k->gtp, sv(buffer));
}

static void send_play(KataGoEngine *k, Move m) {
    char coord[8];
    point_to_gtp(m.point, k->width, k->height, coord);
    send(k, "play %s %s", m.color == STONE_BLACK ? "B" : "W", coord);
}

// " allow B C3,D3,... 60 allow W C3,D3,... 60": KataGo searches only these
// vertices (plus pass) for both players, for the whole search depth.
static void push_region_allow(String *cmd, const BoardRegion *r, i32 width, i32 height) {
    String vertices = string_from(SV("pass"));
    for (i32 y = r->y0; y <= r->y1; ++y) {
        for (i32 x = r->x0; x <= r->x1; ++x) {
            char coord[8];
            point_to_gtp((Point){ (i8)x, (i8)y }, width, height, coord);
            string_pushf(&vertices, ",%s", coord);
        }
    }
    string_pushf(cmd, " allow B %s 60 allow W %s 60", string_cstr(&vertices), string_cstr(&vertices));
    string_free(&vertices);
}

static void send_analyze(KataGoEngine *k) {
    String cmd = string_format("kata-analyze %s interval %d", k->to_play == STONE_WHITE ? "W" : "B",
                               MAX(k->request.interval_centiseconds, 5));
    if (k->request.include_ownership) string_push(&cmd, SV(" ownership true"));
    if (k->request.max_candidates > 0) string_pushf(&cmd, " maxmoves %d", k->request.max_candidates);
    if (k->request.region.active) push_region_allow(&cmd, &k->request.region, k->width, k->height);
    k->analysis_id = gtp_client_send(k->gtp, string_sv(&cmd));
    string_free(&cmd);
}

static bool same_moves(const Move *a, const Move *b, i32 n) {
    for (i32 i = 0; i < n; ++i) {
        if (a[i].color != b[i].color || !point_eq(a[i].point, b[i].point)) return false;
    }
    return true;
}

// ---- vtable ----------------------------------------------------------------------

static StringView kg_name(const Engine *e) { return string_sv(&as_katago_const(e)->name); }
static EngineStatus kg_status(const Engine *e) { return as_katago_const(e)->status; }
static bool kg_is_analyzing(const Engine *e) { return as_katago_const(e)->analyzing; }
static bool kg_is_thinking(const Engine *e) { return as_katago_const(e)->genmove_id > 0; }

static void kg_request_score(Engine *e, u64 position_id) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp) return;
    k->analysis_id = -1;
    k->score_position = position_id;
    k->score_id = send(k, "final_score");
    if (k->analyzing) send_analyze(k);   // keep analysing afterwards
}

static void kg_request_move(Engine *e, const MoveRequest *request) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp || k->genmove_id > 0) return;
    k->analysis_id = -1;   // genmove interrupts any running kata-analyze
    send(k, "time_settings 0 %.2f 1", MAX(request->seconds, 0.1f));
    k->genmove = *request;
    // kata-genmove_analyze streams "info" lines while thinking (so the UI keeps
    // showing candidates / the graph during engine games), then "play <move>".
    k->genmove_id = send(k, "kata-genmove_analyze %s interval %d%s", request->color == STONE_WHITE ? "W" : "B",
                         MAX(k->request.interval_centiseconds, 10), k->request.include_ownership ? " ownership true" : "");
    k->analysis_id = k->genmove_id;
}

static void kg_set_position(Engine *e, const PositionSpec *pos) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp) return;
    if (k->synced && pos->id == k->position_id) return;
    k->analysis_id = -1;   // ignore output from the previous analysis

    bool full_reset = !k->synced || pos->width != k->width || pos->height != k->height || pos->komi != k->komi ||
                      !sv_eq(pos->rules, string_sv(&k->rules)) || pos->setup_count != (i32)k->setup.len ||
                      !same_moves(pos->setup, k->setup.items, pos->setup_count);
    if (full_reset) {
        k->width = pos->width;
        k->height = pos->height;
        if (pos->width == pos->height) send(k, "boardsize %d", pos->width);
        else send(k, "rectangular_boardsize %d %d", pos->width, pos->height);
        send(k, "clear_board");
        k->komi = pos->komi;
        send(k, "komi %g", pos->komi);
        string_clear(&k->rules);
        string_push(&k->rules, pos->rules);
        if (pos->rules.len) send(k, "kata-set-rules " SV_FMT, SV_ARG(pos->rules));
        vec_clear(&k->setup);
        vec_clear(&k->moves);
        if (pos->setup_count > 0) {
            String cmd = string_from(SV("set_position"));
            for (i32 i = 0; i < pos->setup_count; ++i) {
                char coord[8];
                point_to_gtp(pos->setup[i].point, k->width, k->height, coord);
                string_pushf(&cmd, " %s %s", pos->setup[i].color == STONE_BLACK ? "B" : "W", coord);
                vec_push(&k->setup, pos->setup[i]);
            }
            gtp_client_send(k->gtp, string_sv(&cmd));
            string_free(&cmd);
        }
    }

    // Incremental update: undo back to the common prefix, then play forward.
    i32 common = 0;
    i32 limit = MIN((i32)k->moves.len, pos->move_count);
    while (common < limit && same_moves(&k->moves.items[common], &pos->moves[common], 1)) ++common;
    for (i32 i = (i32)k->moves.len; i > common; --i) send(k, "undo");
    k->moves.len = common;
    for (i32 i = common; i < pos->move_count; ++i) {
        send_play(k, pos->moves[i]);
        vec_push(&k->moves, pos->moves[i]);
    }

    k->synced = true;
    k->position_id = pos->id;
    k->to_play = pos->to_play;
    memset(&k->snapshot, 0, sizeof(k->snapshot));
    k->snapshot.position_id = pos->id;
    k->snapshot.to_play = pos->to_play;
    if (k->analyzing) send_analyze(k);
}

static void kg_start_analysis(Engine *e, const AnalysisRequest *request) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp) return;
    k->request = *request;
    k->analyzing = true;
    if (k->synced) send_analyze(k);
}

static void kg_stop_analysis(Engine *e) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp || !k->analyzing) return;
    k->analyzing = false;
    k->analysis_id = -1;
    send(k, "stop");
}

static bool kg_poll(Engine *e, EngineEvent *out) {
    KataGoEngine *k = as_katago(e);
    gtp_event_free(&k->last_event);
    if (!k->gtp) return false;
    GtpEvent ev;
    while (gtp_client_poll(k->gtp, &ev)) {
        k->last_event = ev;
        switch (ev.kind) {
            case GTP_EVENT_RESPONSE:
                if (ev.id == k->score_id && k->score_id > 0) {
                    k->score_id = 0;
                    *out = (EngineEvent){ .kind = ev.success ? ENGINE_EVENT_SCORE : ENGINE_EVENT_ERROR, .status = k->status,
                                          .position_id = k->score_position, .text = sv_trim(string_sv(&ev.text)) };
                    return true;
                }
                if (ev.id == k->genmove_id && k->genmove_id > 0) {
                    k->genmove_id = 0;
                    k->analysis_id = -1;
                    EngineEvent move_event = { .kind = ENGINE_EVENT_MOVE, .status = k->status,
                                               .position_id = k->genmove.position_id, .text = string_sv(&ev.text) };
                    move_event.move = (Move){ k->genmove.color, POINT_PASS };
                    StringView reply = sv_trim(string_sv(&ev.text));
                    // kata-genmove_analyze answers "play <move>" after its info lines.
                    isize play = sv_find(reply, SV("play "));
                    if (play >= 0) reply = sv_trim(sv_drop(reply, play + 5));
                    if (!ev.success) {
                        move_event.kind = ENGINE_EVENT_ERROR;
                    } else if (sv_eq_nocase(reply, SV("resign"))) {
                        move_event.resigned = true;
                    } else if (point_from_gtp(reply, k->width, k->height, &move_event.move.point)) {
                        // The engine already played it on its own board: record it so the
                        // next set_position diff does not replay it.
                        vec_push(&k->moves, move_event.move);
                        k->position_id = 0;
                    }
                    // Analysis resumes on the next set_position (the app always resyncs after a move).
                    *out = move_event;
                    return true;
                }
                if (ev.id == k->version_id && k->status == ENGINE_STATUS_STARTING) {
                    k->status = ENGINE_STATUS_READY;
                    *out = (EngineEvent){ .kind = ENGINE_EVENT_STATUS, .status = k->status, .text = string_sv(&ev.text) };
                    return true;
                }
                if (!ev.success) {
                    *out = (EngineEvent){ .kind = ENGINE_EVENT_ERROR, .status = k->status, .text = string_sv(&ev.text) };
                    return true;
                }
                break;
            case GTP_EVENT_INFO:
                if (ev.id == k->analysis_id && k->analysis_id > 0) {
                    k->snapshot.position_id = k->position_id;
                    k->snapshot.to_play = k->to_play;
                    if (kata_analysis_parse_line(string_sv(&ev.text), k->width, k->height, &k->snapshot)) {
                        *out = (EngineEvent){ .kind = ENGINE_EVENT_ANALYSIS, .status = k->status, .analysis = &k->snapshot };
                        return true;
                    }
                }
                break;
            case GTP_EVENT_STDERR:
                *out = (EngineEvent){ .kind = ENGINE_EVENT_LOG, .status = k->status, .text = string_sv(&ev.text) };
                return true;
            case GTP_EVENT_EXITED:
                k->status = ENGINE_STATUS_FAILED;
                k->analyzing = false;
                *out = (EngineEvent){ .kind = ENGINE_EVENT_STATUS, .status = k->status, .text = SV("engine exited") };
                return true;
        }
        gtp_event_free(&k->last_event);
    }
    return false;
}

static void kg_destroy(Engine *e) {
    KataGoEngine *k = as_katago(e);
    gtp_event_free(&k->last_event);
    gtp_client_stop(k->gtp);
    string_free(&k->name);
    string_free(&k->rules);
    vec_free(&k->setup);
    vec_free(&k->moves);
    free(k);
}

static const EngineVTable KATAGO_VTABLE = {
    .name = kg_name,
    .status = kg_status,
    .set_position = kg_set_position,
    .start_analysis = kg_start_analysis,
    .stop_analysis = kg_stop_analysis,
    .is_analyzing = kg_is_analyzing,
    .request_move = kg_request_move,
    .is_thinking = kg_is_thinking,
    .request_score = kg_request_score,
    .poll = kg_poll,
    .destroy = kg_destroy,
};

Engine *katago_engine_create(const KataGoSettings *settings, String *error) {
    // katago gtp -model M -config C -override-config reportAnalysisWinratesAs=SIDETOMOVE [extra...]
    StringView args[32];
    isize argc = 0;
    args[argc++] = SV("gtp");
    if (settings->model.len) {
        args[argc++] = SV("-model");
        args[argc++] = settings->model;
    }
    if (settings->config.len) {
        args[argc++] = SV("-config");
        args[argc++] = settings->config;
    }
    args[argc++] = SV("-override-config");
    args[argc++] = SV("reportAnalysisWinratesAs=SIDETOMOVE");
    for (isize i = 0; i < settings->extra_arg_count && argc < (isize)countof(args); ++i) args[argc++] = settings->extra_args[i];

    ProcessSpec spec = { settings->program, args, argc, settings->working_dir, settings->launcher, settings->launcher_count };
    GtpClient *gtp = gtp_client_start(&spec, error);
    if (!gtp) return NULL;

    KataGoEngine *k = calloc(1, sizeof(KataGoEngine));
    if (!k) abort();
    k->base.vtable = &KATAGO_VTABLE;
    k->name = string_from(settings->name.len ? settings->name : SV("KataGo"));
    k->gtp = gtp;
    k->status = ENGINE_STATUS_STARTING;
    k->analysis_id = -1;
    k->version_id = gtp_client_send(gtp, SV("version"));
    log_info("started engine " SV_FMT, SV_ARG(settings->program));
    return &k->base;
}
