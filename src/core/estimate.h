// estimate.h - territory estimate and confidence from KataGo's ownership map
// (ported from BadukCast's Live/ownership).
//
// Each point counts for the side that probably owns it: "secure" when the
// ownership is at least 0.8, "likely" from 0.4, else it is unclaimed. Area
// counting adds up ownership over the whole board; territory counting skips a
// side's own stones and counts a secure point on an opponent's (dead) stone twice
// (the point and the prisoner). The totals are then nudged to agree with
// KataGo's score lead (komi included), so the estimate never contradicts it.
#pragma once

#include "core/board.h"

typedef struct TerritoryEstimate {
    f32 black, white;                  // estimated points (white includes komi), agreeing with the score lead
    f32 black_secure, white_secure;    // points at >= 0.8 (incl. the score-lead adjustment; white incl. komi)
    f32 black_likely, white_likely;    // points at 0.4 .. 0.8
    f32 unclaimed;                     // points below 0.4 (and, territory counting, own stones)
    f32 decided;                       // share of the board that is secure, 0..1 (the confidence)
} TerritoryEstimate;

// ownership: +1 = Black owns, -1 = White, row-major like board->cells.
// territory_rules: Japanese / Korean counting (else area). score_lead: Black's lead.
TerritoryEstimate territory_estimate(const Board *board, const f32 *black_ownership, f32 komi, f32 score_lead, bool territory_rules);
