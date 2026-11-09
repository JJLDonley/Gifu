// analysis.h - engine-neutral analysis results.
// All values are from the perspective of the side to move (`to_play`).
#pragma once

#include "core/go.h"

enum {
    ANALYSIS_MAX_CANDIDATES = 48,
    ANALYSIS_MAX_PV = 40,
};

typedef struct Candidate {
    Point move;
    i32 visits;
    i32 order;
    f32 winrate;      // 0..1
    f32 score_lead;
    f32 score_stdev;
    f32 prior;
    f32 lcb;
    i32 pv_len;
    Point pv[ANALYSIS_MAX_PV];
} Candidate;

typedef struct AnalysisSnapshot {
    u64 position_id;   // which synced position this belongs to
    Stone to_play;
    i32 width, height;
    i32 total_visits;
    i32 candidate_count;
    Candidate candidates[ANALYSIS_MAX_CANDIDATES];
    bool has_ownership;
    f32 ownership[BOARD_MAX_POINTS];   // +1 = owned by side to move
} AnalysisSnapshot;

static inline const Candidate *analysis_best(const AnalysisSnapshot *a) {
    return a->candidate_count > 0 ? &a->candidates[0] : NULL;
}
static inline f32 analysis_black_winrate(const AnalysisSnapshot *a, f32 side_to_move_winrate) {
    return a->to_play == STONE_WHITE ? 1.0f - side_to_move_winrate : side_to_move_winrate;
}
static inline f32 analysis_black_score(const AnalysisSnapshot *a, f32 side_to_move_score) {
    return a->to_play == STONE_WHITE ? -side_to_move_score : side_to_move_score;
}

// Parses one KataGo `kata-analyze` output line ("info move ... [ownership ...]").
// Candidates are sorted by `order`. Returns false if nothing usable was found.
NODISCARD bool kata_analysis_parse_line(StringView line, i32 width, i32 height, AnalysisSnapshot *out);
