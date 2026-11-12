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
    // Board commands awaiting their reply (FIFO of ids): KataGo refusing one
    // (e.g. "? illegal move") means its board differs from ours, so it is rebuilt
    // from scratch from the last position (Sabaki does the same).
    i32 sync_ids[256];
    i32 sync_head, sync_count;
    PositionSpec *last_pos;
    // A position asked for while a genmove runs: KataGo queues commands until its move
    // is placed, so our record of its moves would end up in the wrong order. Applied
    // when the move comes back.
    bool sync_deferred;
    i32 rebuilds;   // consecutive rebuilds that failed again (gives up after a few)
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

    // Human policy.
    i32 human_ids[64];          // FIFO of outstanding kata-raw-human-nn commands
    u64 human_positions[64];
    i32 human_head, human_count;
    char human_profile[40];     // current humanSLProfile
    bool has_human_model;
    i32 max_visits_id;          // kata-get-param maxVisits (restored after human-style moves)
    char default_max_visits[24];
    bool human_move;            // the pending genmove uses human-style parameters
    HumanPolicy human;

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

static void track_sync(KataGoEngine *k, i32 id) {
    if (id <= 0) return;
    if (k->sync_count == (i32)countof(k->sync_ids)) {   // full: forget the oldest
        k->sync_head = (k->sync_head + 1) % (i32)countof(k->sync_ids);
        k->sync_count--;
    }
    k->sync_ids[(k->sync_head + k->sync_count) % (i32)countof(k->sync_ids)] = id;
    k->sync_count++;
}

// A command that changes KataGo's board; its reply is checked.
static i32 send_sync(KataGoEngine *k, const char *fmt, ...) PRINTF_LIKE(2, 3);
static i32 send_sync(KataGoEngine *k, const char *fmt, ...) {
    char buffer[8192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    i32 id = gtp_client_send(k->gtp, sv(buffer));
    track_sync(k, id);
    return id;
}

static void send_play(KataGoEngine *k, Move m) {
    char coord[8];
    point_to_gtp(m.point, k->width, k->height, coord);
    send_sync(k, "play %s %s", m.color == STONE_BLACK ? "B" : "W", coord);
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

static void kg_request_human_policy(Engine *e, u64 position_id) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp) return;
    k->analysis_id = -1;
    if (k->human_count == (i32)countof(k->human_ids)) return;   // caller is flooding; drop
    i32 slot = (k->human_head + k->human_count) % (i32)countof(k->human_ids);
    k->human_positions[slot] = position_id;
    k->human_ids[slot] = send(k, "kata-raw-human-nn 0");   // ("all" would print 8 separate blocks)
    k->human_count++;
    if (k->analyzing) send_analyze(k);
}

static void kg_set_human_profile(Engine *e, StringView profile) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp || !human_profile_valid(profile) || sv_eq(profile, sv(k->human_profile))) return;
    snprintf(k->human_profile, sizeof(k->human_profile), SV_FMT, SV_ARG(profile));
    send(k, "kata-set-param humanSLProfile %s", k->human_profile);
}

static void kg_request_move(Engine *e, const MoveRequest *request) {
    KataGoEngine *k = as_katago(e);
    if (!k->gtp || k->genmove_id > 0) return;
    k->analysis_id = -1;   // genmove interrupts any running kata-analyze
    // A byo-yomi of `seconds` per move, or none: "time_settings 0 1 0" is GTP's
    // "no time limit", so KataGo searches its configured maxVisits (full strength).
    // Not "0 0 0": KataGo takes that as no time at all (measured: ~0.05 s, 100 visits).
    if (request->seconds > 0) send(k, "time_settings 0 %.2f 1", MAX(request->seconds, 0.1f));
    else send(k, "time_settings 0 1 0");
    // The visit limit for this move (human-style moves set their own below, then restore).
    if (request->max_visits > 0 && !request->human_style) send(k, "kata-set-param maxVisits %d", request->max_visits);
    k->genmove = *request;
    // Human style: pick the move from the human policy (KataGo's gtp_human*_example.cfg
    // settings, switched in for this move only).
    k->human_move = request->human_style && k->has_human_model && k->default_max_visits[0];
    if (k->human_move) {
        // The AI's own strength for this move; the overlay's profile comes back after it.
        if (request->human_profile[0] && strcmp(request->human_profile, k->human_profile) != 0) {
            send(k, "kata-set-param humanSLProfile %s", request->human_profile);
        }
        send(k, "kata-set-param humanSLChosenMoveProp 1.0");
        send(k, "kata-set-param humanSLChosenMoveIgnorePass true");
        send(k, "kata-set-param humanSLChosenMovePiklLambda 100000000");
        send(k, "kata-set-param maxVisits 40");
    }
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
    if (pos != k->last_pos) {   // kept for rebuilding the board if KataGo refuses a command
        if (!k->last_pos) k->last_pos = malloc(sizeof(PositionSpec));
        if (!k->last_pos) abort();
        memcpy(k->last_pos, pos, sizeof(PositionSpec));
    }
    if (k->genmove_id > 0) {   // KataGo is placing its own move: sync once it has
        k->sync_deferred = true;
        return;
    }
    k->analysis_id = -1;   // ignore output from the previous analysis

    bool full_reset = !k->synced || pos->width != k->width || pos->height != k->height || pos->komi != k->komi ||
                      !sv_eq(pos->rules, string_sv(&k->rules)) || pos->setup_count != (i32)k->setup.len ||
                      !same_moves(pos->setup, k->setup.items, pos->setup_count);
    if (full_reset) {
        k->width = pos->width;
        k->height = pos->height;
        if (pos->width == pos->height) send_sync(k, "boardsize %d", pos->width);
        else send_sync(k, "rectangular_boardsize %d %d", pos->width, pos->height);
        send_sync(k, "clear_board");
        k->komi = pos->komi;
        send(k, "komi %g", pos->komi);
        string_clear(&k->rules);
        string_push(&k->rules, pos->rules);
        if (pos->rules.len) send(k, "kata-set-rules " SV_FMT, SV_ARG(pos->rules));
        vec_clear(&k->setup);
        vec_clear(&k->moves);
        if (pos->setup_count > 0) {
            // Handicap stones go in as handicap (GTP set_free_handicap): with set_position
            // KataGo keeps Black to move and does not count them as handicap.
            String cmd = string_from(pos->handicap ? SV("set_free_handicap") : SV("set_position"));
            for (i32 i = 0; i < pos->setup_count; ++i) {
                char coord[8];
                point_to_gtp(pos->setup[i].point, k->width, k->height, coord);
                if (pos->handicap) string_pushf(&cmd, " %s", coord);
                else string_pushf(&cmd, " %s %s", pos->setup[i].color == STONE_BLACK ? "B" : "W", coord);
                vec_push(&k->setup, pos->setup[i]);
            }
            track_sync(k, gtp_client_send(k->gtp, string_sv(&cmd)));
            string_free(&cmd);
        }
    }

    // Incremental update: undo back to the common prefix, then play forward.
    i32 common = 0;
    i32 limit = MIN((i32)k->moves.len, pos->move_count);
    while (common < limit && same_moves(&k->moves.items[common], &pos->moves[common], 1)) ++common;
    for (i32 i = (i32)k->moves.len; i > common; --i) send_sync(k, "undo");
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

static void kg_set_position(Engine *e, const PositionSpec *pos);

// KataGo refused a board command: its board is not ours any more. Rebuild it from
// scratch (clear_board, handicap, every move) from the last position we sent.
static void resync(KataGoEngine *k, StringView reply) {
    log_warn("KataGo refused a board command (" SV_FMT "): rebuilding its board", SV_ARG(reply));
    k->sync_count = 0;
    k->synced = false;
    k->position_id = 0;
    if (!k->last_pos || ++k->rebuilds > 3) {   // the same position keeps failing: stop trying
        log_warn("KataGo: giving up rebuilding the board");
        return;
    }
    kg_set_position(&k->base, k->last_pos);   // also restarts kata-analyze if it was running
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
                // Board commands are answered in order: drop those already answered.
                while (k->sync_count > 0 && k->sync_ids[k->sync_head] < ev.id) {
                    k->sync_head = (k->sync_head + 1) % (i32)countof(k->sync_ids);
                    k->sync_count--;
                }
                if (k->sync_count > 0 && k->sync_ids[k->sync_head] == ev.id) {
                    k->sync_head = (k->sync_head + 1) % (i32)countof(k->sync_ids);
                    k->sync_count--;
                    if (!ev.success) {
                        resync(k, sv_trim(string_sv(&ev.text)));
                        *out = (EngineEvent){ .kind = ENGINE_EVENT_LOG, .status = k->status,
                                              .text = SV("KataGo refused a board command: rebuilding its board") };
                        return true;
                    }
                    if (k->rebuilds > 0 && k->sync_count == 0) k->rebuilds = 0;   // the rebuild went through
                    break;
                }
                if (ev.id == k->score_id && k->score_id > 0) {
                    k->score_id = 0;
                    *out = (EngineEvent){ .kind = ev.success ? ENGINE_EVENT_SCORE : ENGINE_EVENT_ERROR, .status = k->status,
                                          .position_id = k->score_position, .text = sv_trim(string_sv(&ev.text)) };
                    return true;
                }
                if (k->human_count > 0 && ev.id == k->human_ids[k->human_head]) {
                    u64 position = k->human_positions[k->human_head];
                    k->human_head = (k->human_head + 1) % (i32)countof(k->human_ids);
                    k->human_count--;
                    if (ev.success && kata_raw_nn_parse(string_sv(&ev.text), k->width, k->height, &k->human)) {
                        k->human.position_id = position;
                        *out = (EngineEvent){ .kind = ENGINE_EVENT_HUMAN_POLICY, .status = k->status, .human_policy = &k->human,
                                              .position_id = position };
                    } else {
                        *out = (EngineEvent){ .kind = ENGINE_EVENT_ERROR, .status = k->status, .text = string_sv(&ev.text) };
                    }
                    return true;
                }
                if (ev.id == k->max_visits_id && k->max_visits_id > 0) {
                    k->max_visits_id = 0;
                    StringView v = sv_trim(string_sv(&ev.text));
                    if (ev.success && v.len > 0 && v.len < (isize)sizeof(k->default_max_visits)) {
                        snprintf(k->default_max_visits, sizeof(k->default_max_visits), SV_FMT, SV_ARG(v));
                    }
                    break;
                }
                if (ev.id == k->genmove_id && k->genmove_id > 0) {
                    k->genmove_id = 0;
                    if (k->human_move) {   // back to normal search
                        k->human_move = false;
                        if (k->genmove.human_profile[0] && strcmp(k->genmove.human_profile, k->human_profile) != 0) {
                            send(k, "kata-set-param humanSLProfile %s", k->human_profile);
                        }
                        send(k, "kata-set-param humanSLChosenMoveProp 0.0");
                        send(k, "kata-set-param maxVisits %s", k->default_max_visits);
                    }
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
                    if (k->sync_deferred) {   // a position came in while KataGo was thinking
                        k->sync_deferred = false;
                        kg_set_position(&k->base, k->last_pos);
                    }
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
    free(k->last_pos);
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
    .request_human_policy = kg_request_human_policy,
    .set_human_profile = kg_set_human_profile,
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
    if (settings->human_model.len) {
        args[argc++] = SV("-human-model");
        args[argc++] = settings->human_model;
    }
    // One -override-config with every override, comma-separated.
    String overrides = string_from(SV("reportAnalysisWinratesAs=SIDETOMOVE"));
    bool human = settings->human_model.len && human_profile_valid(settings->human_profile);
    if (human) string_pushf(&overrides, ",humanSLProfile=" SV_FMT, SV_ARG(settings->human_profile));
    string_push(&overrides, settings->overrides);
    log_info("katago overrides: " SV_FMT, SV_ARG(string_sv(&overrides)));
    args[argc++] = SV("-override-config");
    args[argc++] = string_sv(&overrides);
    for (isize i = 0; i < settings->extra_arg_count && argc < (isize)countof(args); ++i) args[argc++] = settings->extra_args[i];

    ProcessSpec spec = { settings->program, args, argc, settings->working_dir, settings->launcher, settings->launcher_count };
    GtpClient *gtp = gtp_client_start(&spec, error);
    string_free(&overrides);
    if (!gtp) return NULL;

    KataGoEngine *k = calloc(1, sizeof(KataGoEngine));
    if (!k) abort();
    k->base.vtable = &KATAGO_VTABLE;
    k->name = string_from(settings->name.len ? settings->name : SV("KataGo"));
    k->gtp = gtp;
    k->status = ENGINE_STATUS_STARTING;
    k->analysis_id = -1;
    if (human) snprintf(k->human_profile, sizeof(k->human_profile), SV_FMT, SV_ARG(settings->human_profile));
    k->has_human_model = settings->human_model.len > 0;
    if (k->has_human_model) k->max_visits_id = gtp_client_send(gtp, SV("kata-get-param maxVisits"));
    k->version_id = gtp_client_send(gtp, SV("version"));
    log_info("started engine " SV_FMT, SV_ARG(settings->program));
    return &k->base;
}
