// ai_style.h - the engine's playing style (Settings -> AI play -> Style).
//
// Strongest plays KataGo's move; Human plays like a human of the chosen strength
// (KataGo's human network: rank, pre-AI era or professional year). The others
// pick, among KataGo's candidates that lose at most AI_STYLE_BUDGET points
// against the best one (and were searched enough to trust), the one that fits
// the style best; ties go to KataGo's order.
#pragma once

#include "core/board.h"
#include "engine/analysis.h"

typedef enum AiStyle {
    AI_STYLE_STRONGEST,
    AI_STYLE_HUMAN,
    AI_STYLE_TERRITORIAL,   // the 3rd line, then the 2nd
    AI_STYLE_INFLUENCE,     // the 4th line and higher
    AI_STYLE_FIGHTING,      // contact with the opponent
    AI_STYLE_CALM,          // no contact
} AiStyle;

#define AI_STYLE_BUDGET 1.0f   // points a style move may give up
enum { AI_STYLE_MIN_VISIT_PERMILLE = 30 };   // candidates with < 3% of the best one's visits are not trusted

AiStyle ai_style_from(StringView id);   // "strongest", "human", "territorial", ...
// True for the styles that pick among candidates (not a plain engine move).
static inline bool ai_style_picks(AiStyle s) { return s >= AI_STYLE_TERRITORIAL; }
// The candidate to play (index into a->candidates), or -1 when there is none.
i32 ai_style_pick(AiStyle style, const AnalysisSnapshot *a, const Board *board);
