#include "engine/gtp_engine.h"

#include "base/log.h"
#include "base/vec.h"
#include "engine/gtp_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef Vec(Move) MoveVec;

typedef struct GtpEngine {
    Engine base;   // must be first
    String name;
    GtpClient *gtp;
    EngineStatus status;
    i32 name_id;

    // What the engine has on its board.
    bool synced;
    i32 width, height;
    f32 komi;
    MoveVec setup, moves;   // setup stones are sent as plays (plain GTP has no setup command)
    u64 position_id;

    i32 genmove_id;
    MoveRequest genmove;
    i32 score_id;
    u64 score_position;
    GtpEvent last_event;   // owns text referenced by the last EngineEvent
} GtpEngine;

static GtpEngine *as_gtp(Engine *e) { return (GtpEngine *)e; }
static const GtpEngine *as_gtp_const(const Engine *e) { return (const GtpEngine *)e; }

static i32 send(GtpEngine *g, const char *fmt, ...) PRINTF_LIKE(2, 3);
static i32 send(GtpEngine *g, const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return gtp_client_send(g->gtp, sv(buffer));
}

static void send_play(GtpEngine *g, Move m) {
    char coord[8];
    point_to_gtp(m.point, g->width, g->height, coord);
    send(g, "play %s %s", m.color == STONE_BLACK ? "B" : "W", coord);
}

static bool same_moves(const Move *a, const Move *b, i32 n) {
    for (i32 i = 0; i < n; ++i) {
        if (a[i].color != b[i].color || a[i].point.x != b[i].point.x || a[i].point.y != b[i].point.y) return false;
    }
    return true;
}

static StringView ge_name(const Engine *e) { return string_sv(&as_gtp_const(e)->name); }
static EngineStatus ge_status(const Engine *e) { return as_gtp_const(e)->status; }
static bool ge_is_analyzing(const Engine *e) {
    UNUSED(e);
    return false;
}
static bool ge_is_thinking(const Engine *e) { return as_gtp_const(e)->genmove_id > 0; }
static void ge_start_analysis(Engine *e, const AnalysisRequest *request) {
    UNUSED(e), UNUSED(request);   // a playing engine: KataGo analyses
}
static void ge_stop_analysis(Engine *e) { UNUSED(e); }

static void ge_set_position(Engine *e, const PositionSpec *pos) {
    GtpEngine *g = as_gtp(e);
    if (!g->gtp || (g->synced && pos->id == g->position_id)) return;
    bool full_reset = !g->synced || pos->width != g->width || pos->height != g->height || pos->komi != g->komi ||
                      pos->setup_count != (i32)g->setup.len || !same_moves(pos->setup, g->setup.items, pos->setup_count);
    if (full_reset) {
        g->width = pos->width, g->height = pos->height, g->komi = pos->komi;
        send(g, "boardsize %d", pos->width);   // square boards only in plain GTP
        send(g, "clear_board");
        send(g, "komi %g", pos->komi);
        vec_clear(&g->setup);
        vec_clear(&g->moves);
        for (i32 i = 0; i < pos->setup_count; ++i) {
            send_play(g, pos->setup[i]);
            vec_push(&g->setup, pos->setup[i]);
        }
    }
    // Undo back to the common prefix, then play forward.
    i32 common = 0, limit = MIN((i32)g->moves.len, pos->move_count);
    while (common < limit && same_moves(&g->moves.items[common], &pos->moves[common], 1)) ++common;
    for (i32 i = (i32)g->moves.len; i > common; --i) send(g, "undo");
    g->moves.len = common;
    for (i32 i = common; i < pos->move_count; ++i) {
        send_play(g, pos->moves[i]);
        vec_push(&g->moves, pos->moves[i]);
    }
    g->synced = true;
    g->position_id = pos->id;
}

static void ge_request_move(Engine *e, const MoveRequest *request) {
    GtpEngine *g = as_gtp(e);
    if (!g->gtp || g->genmove_id > 0) return;
    // Byo-yomi of one move in `seconds`: engines without time controls ignore it.
    send(g, "time_settings 0 %d 1", (i32)MAX(request->seconds + 0.5f, 1.0f));
    g->genmove = *request;
    g->genmove_id = send(g, "genmove %s", request->color == STONE_WHITE ? "W" : "B");
}

static void ge_request_score(Engine *e, u64 position_id) {
    GtpEngine *g = as_gtp(e);
    if (!g->gtp) return;
    g->score_position = position_id;
    g->score_id = send(g, "final_score");
}

static bool ge_poll(Engine *e, EngineEvent *out) {
    GtpEngine *g = as_gtp(e);
    gtp_event_free(&g->last_event);
    if (!g->gtp) return false;
    GtpEvent ev;
    while (gtp_client_poll(g->gtp, &ev)) {
        g->last_event = ev;
        switch (ev.kind) {
            case GTP_EVENT_RESPONSE:
                if (ev.id == g->name_id && g->status == ENGINE_STATUS_STARTING) {
                    g->status = ev.success ? ENGINE_STATUS_READY : ENGINE_STATUS_FAILED;
                    *out = (EngineEvent){ .kind = ENGINE_EVENT_STATUS, .status = g->status, .text = string_sv(&ev.text) };
                    return true;
                }
                if (ev.id == g->score_id && g->score_id > 0) {
                    g->score_id = 0;
                    *out = (EngineEvent){ .kind = ev.success ? ENGINE_EVENT_SCORE : ENGINE_EVENT_ERROR, .status = g->status,
                                          .position_id = g->score_position, .text = sv_trim(string_sv(&ev.text)) };
                    return true;
                }
                if (ev.id == g->genmove_id && g->genmove_id > 0) {
                    g->genmove_id = 0;
                    EngineEvent move_event = { .kind = ENGINE_EVENT_MOVE, .status = g->status, .position_id = g->genmove.position_id,
                                               .text = string_sv(&ev.text) };
                    move_event.move = (Move){ g->genmove.color, POINT_PASS };
                    StringView reply = sv_trim(string_sv(&ev.text));
                    if (!ev.success) {
                        move_event.kind = ENGINE_EVENT_ERROR;
                    } else if (sv_eq_nocase(reply, SV("resign"))) {
                        move_event.resigned = true;
                    } else if (point_from_gtp(reply, g->width, g->height, &move_event.move.point)) {
                        vec_push(&g->moves, move_event.move);   // on its board already: not replayed by the next sync
                        g->position_id = 0;
                    }
                    *out = move_event;
                    return true;
                }
                if (!ev.success) {
                    *out = (EngineEvent){ .kind = ENGINE_EVENT_ERROR, .status = g->status, .text = string_sv(&ev.text) };
                    return true;
                }
                break;
            case GTP_EVENT_STDERR:
                *out = (EngineEvent){ .kind = ENGINE_EVENT_LOG, .status = g->status, .text = string_sv(&ev.text) };
                return true;
            case GTP_EVENT_EXITED:
                g->status = ENGINE_STATUS_FAILED;
                *out = (EngineEvent){ .kind = ENGINE_EVENT_STATUS, .status = g->status, .text = SV("engine exited") };
                return true;
            case GTP_EVENT_INFO:
                break;
        }
        gtp_event_free(&g->last_event);
    }
    return false;
}

static void ge_destroy(Engine *e) {
    GtpEngine *g = as_gtp(e);
    gtp_event_free(&g->last_event);
    gtp_client_stop(g->gtp);
    string_free(&g->name);
    vec_free(&g->setup);
    vec_free(&g->moves);
    free(g);
}

static const EngineVTable GTP_VTABLE = {
    .name = ge_name,
    .status = ge_status,
    .set_position = ge_set_position,
    .start_analysis = ge_start_analysis,
    .stop_analysis = ge_stop_analysis,
    .is_analyzing = ge_is_analyzing,
    .request_move = ge_request_move,
    .is_thinking = ge_is_thinking,
    .request_score = ge_request_score,
    .poll = ge_poll,
    .destroy = ge_destroy,
};

Engine *gtp_engine_create(const GtpEngineSettings *s, String *error) {
    ProcessSpec spec = { s->program, s->args, s->arg_count, s->working_dir, s->launcher, s->launcher_count };
    GtpClient *gtp = gtp_client_start(&spec, error);
    if (!gtp) return NULL;
    GtpEngine *g = calloc(1, sizeof(GtpEngine));
    if (!g) abort();
    g->base.vtable = &GTP_VTABLE;
    g->name = string_from(s->name.len ? s->name : SV("GTP engine"));
    g->gtp = gtp;
    g->status = ENGINE_STATUS_STARTING;
    g->name_id = gtp_client_send(gtp, SV("name"));   // ready once it answers
    log_info("started GTP engine " SV_FMT, SV_ARG(s->program));
    return &g->base;
}
