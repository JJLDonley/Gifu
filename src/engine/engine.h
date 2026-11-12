// engine.h - the Engine interface (dependency-inversion boundary).
//
// The app talks only to `Engine`; concrete engines (KataGo GTP today, KataGo
// analysis-JSON / remote SSH / other GTP engines later) implement EngineVTable.
#pragma once

#include "core/game_tree.h"
#include "engine/analysis.h"

typedef enum EngineStatus {
    ENGINE_STATUS_STOPPED,
    ENGINE_STATUS_STARTING,
    ENGINE_STATUS_READY,
    ENGINE_STATUS_FAILED,
} EngineStatus;

// A position described the way engines want it: optional setup stones,
// followed by a move sequence.
typedef struct PositionSpec {
    u64 id;
    i32 width, height;
    f32 komi;
    StringView rules;   // KataGo rule name, e.g. "chinese", "japanese", "tromp-taylor"
    Stone to_play;
    i32 setup_count;
    Move setup[BOARD_MAX_POINTS];
    // The setup is handicap stones (the game starts from black stones only, two or
    // more): engines must get them as handicap (GTP set_free_handicap), so that
    // White moves first and gets the handicap bonus (KataGo's whiteHandicapBonus).
    bool handicap;
    i32 move_count;
    Move moves[1024];
} PositionSpec;

// Inclusive board rectangle (x0 <= x1, y0 <= y1).
typedef struct BoardRegion {
    bool active;
    i8 x0, y0, x1, y1;
} BoardRegion;

typedef struct AnalysisRequest {
    i32 interval_centiseconds;
    bool include_ownership;
    i32 max_candidates;   // 0 = engine default
    BoardRegion region;   // when active, only moves inside it are searched (tsumego)
} AnalysisRequest;

typedef struct MoveRequest {
    u64 position_id;          // the position the move is for (from set_position)
    Stone color;
    f32 seconds;              // thinking time per move
    bool human_style;         // play like a human of the current humanSLProfile (engines with a human model)
} MoveRequest;

typedef enum EngineEventKind {
    ENGINE_EVENT_STATUS,     // status changed (see engine_status)
    ENGINE_EVENT_MOVE,       // `move` answers a request_move (`resigned` if the engine resigned)
    ENGINE_EVENT_SCORE,      // `text` answers request_score, e.g. "B+3.5" or "0"
    ENGINE_EVENT_ANALYSIS,   // `analysis` points at the latest snapshot
    ENGINE_EVENT_HUMAN_POLICY,   // `human_policy` answers request_human_policy
    ENGINE_EVENT_LOG,        // `text` is one log line
    ENGINE_EVENT_ERROR,      // `text` describes a failed command
} EngineEventKind;

typedef struct EngineEvent {
    EngineEventKind kind;
    EngineStatus status;
    const AnalysisSnapshot *analysis;   // valid until the next poll
    const HumanPolicy *human_policy;    // valid until the next poll
    StringView text;                    // valid until the next poll
    Move move;
    u64 position_id;
    bool resigned;
} EngineEvent;

typedef struct Engine Engine;

typedef struct EngineVTable {
    StringView (*name)(const Engine *engine);
    EngineStatus (*status)(const Engine *engine);
    void (*set_position)(Engine *engine, const PositionSpec *position);
    void (*start_analysis)(Engine *engine, const AnalysisRequest *request);
    void (*stop_analysis)(Engine *engine);
    bool (*is_analyzing)(const Engine *engine);
    void (*request_move)(Engine *engine, const MoveRequest *request);
    bool (*is_thinking)(const Engine *engine);
    void (*request_score)(Engine *engine, u64 position_id);
    // Human SL network policy for the synced position (engines without a human model answer with an error).
    // Requests are answered in order, one ENGINE_EVENT_HUMAN_POLICY each.
    void (*request_human_policy)(Engine *engine, u64 position_id);
    // Switches humanSLProfile (e.g. "rank_5k") in place; applies to later requests and searches.
    void (*set_human_profile)(Engine *engine, StringView profile);
    bool (*poll)(Engine *engine, EngineEvent *out);
    void (*destroy)(Engine *engine);
} EngineVTable;

struct Engine {
    const EngineVTable *vtable;
};

static inline StringView engine_name(const Engine *e) { return e->vtable->name(e); }
static inline EngineStatus engine_status(const Engine *e) { return e->vtable->status(e); }
static inline void engine_set_position(Engine *e, const PositionSpec *p) { e->vtable->set_position(e, p); }
static inline void engine_start_analysis(Engine *e, const AnalysisRequest *r) { e->vtable->start_analysis(e, r); }
static inline void engine_stop_analysis(Engine *e) { e->vtable->stop_analysis(e); }
static inline bool engine_is_analyzing(const Engine *e) { return e->vtable->is_analyzing(e); }
static inline bool engine_poll(Engine *e, EngineEvent *out) { return e->vtable->poll(e, out); }
static inline void engine_request_move(Engine *e, const MoveRequest *r) { e->vtable->request_move(e, r); }
static inline bool engine_is_thinking(const Engine *e) { return e->vtable->is_thinking(e); }
static inline void engine_request_score(Engine *e, u64 position_id) { e->vtable->request_score(e, position_id); }
static inline void engine_request_human_policy(Engine *e, u64 position_id) {
    if (e->vtable->request_human_policy) e->vtable->request_human_policy(e, position_id);
}
static inline void engine_set_human_profile(Engine *e, StringView profile) {
    if (e->vtable->set_human_profile) e->vtable->set_human_profile(e, profile);
}
// True for "rank_5k", "preaz_1d", "proyear_1990", "rank_3k_1d"... (safe to put in a config override).
bool human_profile_valid(StringView profile);
// `base` ("rank_5k", "preaz_1d", "proyear_1990") with White at `white_rank` ("1d";
// empty = same) -> "rank_5k_1d". Profiles without ranks are returned unchanged.
void human_profile_combine(StringView base, StringView white_rank, char *out, isize capacity);
static inline void engine_destroy(Engine *e) {
    if (e) e->vtable->destroy(e);
}

const char *engine_status_name(EngineStatus status);

// Builds the engine-facing description of `node` (root..node path).
void position_spec_from_node(const GameTree *tree, const GameNode *node, StringView rules, PositionSpec *out);
// The nodes behind that spec's moves, in the same order (out[i] played moves[i]).
i32 position_spec_move_nodes(const GameNode *node, const GameNode **out, i32 capacity);
