// batch_analyzer.h - whole-game analysis interface (dependency-inversion boundary).
//
// Unlike the interactive Engine (one position, streaming), a BatchAnalyzer
// takes whole games and answers per move, letting the engine batch many
// positions on the GPU. Implementation: KataGo's JSON `analysis` mode.
#pragma once

#include "engine/engine.h"

typedef struct AnalysisJob {
    StringView id;               // echoed back in results
    i32 width, height;
    f32 komi;
    StringView rules;
    const Move *setup;           // initial stones
    i32 setup_count;
    const Move *moves;           // played after the setup
    i32 move_count;
    Stone first_player;          // who plays moves[0] (matters with setup stones)
    i32 max_visits;
} AnalysisJob;

typedef struct TurnResult {
    String job_id;               // owned; free with turn_result_free
    i32 turn;                    // 0 = before moves[0]; k = after k moves
    bool ok;
    NodeEval eval;               // Black's perspective, with top moves
    String error;
} TurnResult;

typedef struct BatchAnalyzer BatchAnalyzer;

typedef struct BatchAnalyzerVTable {
    // Queues a game: results arrive for turns 0..move_count (in any order).
    bool (*submit)(BatchAnalyzer *a, const AnalysisJob *job);
    bool (*poll)(BatchAnalyzer *a, TurnResult *out);
    bool (*alive)(const BatchAnalyzer *a);
    void (*cancel)(BatchAnalyzer *a, StringView job_id);   // stop work on a job
    void (*destroy)(BatchAnalyzer *a);
} BatchAnalyzerVTable;

struct BatchAnalyzer {
    const BatchAnalyzerVTable *vtable;
};

static inline bool batch_analyzer_submit(BatchAnalyzer *a, const AnalysisJob *job) { return a->vtable->submit(a, job); }
static inline bool batch_analyzer_poll(BatchAnalyzer *a, TurnResult *out) { return a->vtable->poll(a, out); }
static inline bool batch_analyzer_alive(const BatchAnalyzer *a) { return a->vtable->alive(a); }
static inline void batch_analyzer_cancel(BatchAnalyzer *a, StringView job_id) { a->vtable->cancel(a, job_id); }
static inline void batch_analyzer_destroy(BatchAnalyzer *a) {
    if (a) a->vtable->destroy(a);
}
void turn_result_free(TurnResult *r);

// KataGo JSON analysis engine (`katago analysis -model M -config C`).
typedef struct KataGoAnalyzerSettings {
    StringView program, model, config, working_dir;
    const StringView *launcher;   // optional prefix (ssh ...)
    isize launcher_count;
    StringView overrides;         // extra ",key=value" pairs (katago_tuning_overrides)
} KataGoAnalyzerSettings;
BatchAnalyzer *katago_analyzer_create(const KataGoAnalyzerSettings *settings, String *error);

// Builds the JSON query line for a job (exposed for tests).
String katago_analysis_query(const AnalysisJob *job);
// Parses one response line (exposed for tests). Returns false for non-result lines.
bool katago_analysis_parse_response(StringView line, i32 width, i32 height, TurnResult *out);
