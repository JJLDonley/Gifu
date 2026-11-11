#include "core/estimate.h"

#include <math.h>

TerritoryEstimate territory_estimate(const Board *board, const f32 *own, f32 komi, f32 score_lead, bool territory_rules) {
    TerritoryEstimate e = { 0 };
    i32 n = board->width * board->height;
    e.white = komi;
    e.white_secure = komi;
    i32 secure_points = 0;
    for (i32 i = 0; i < n; ++i) {
        f32 v = own[i], a = fabsf(v);
        bool black = v >= 0;
        Stone stone = (Stone)board->cells[i];
        if (a >= 0.8f) ++secure_points;
        if (territory_rules) {
            Stone mine = black ? STONE_BLACK : STONE_WHITE, theirs = black ? STONE_WHITE : STONE_BLACK;
            if (a < 0.4f || stone == mine) {   // own stones are not territory
                e.unclaimed += 1;
                continue;
            }
            f32 *total = black ? &e.black : &e.white;
            if (a >= 0.8f) {
                f32 points = stone == theirs ? 2.0f : 1.0f;   // a dead stone: the point and the prisoner
                *total += points;
                *(black ? &e.black_secure : &e.white_secure) += points;
            } else {
                *(black ? &e.black_likely : &e.white_likely) += 1;
            }
        } else {
            if (black) e.black += a;
            else e.white += a;
            if (a >= 0.8f) *(black ? &e.black_secure : &e.white_secure) += 1;
            else if (a >= 0.4f) *(black ? &e.black_likely : &e.white_likely) += 1;
            else e.unclaimed += 1;
        }
    }
    // Agree with the score lead: the difference goes to the side KataGo says is ahead.
    f32 adjustment = score_lead - (e.black - e.white);
    if (fabsf(adjustment) > 0.1f) {
        if (adjustment > 0) e.black += adjustment, e.black_secure += adjustment;
        else e.white -= adjustment, e.white_secure -= adjustment;
    }
    e.decided = n > 0 ? (f32)secure_points / (f32)n : 0.0f;
    return e;
}
