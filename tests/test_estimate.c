#include "test.h"

#include "core/estimate.h"
#include "core/human_winrate.h"

#include <math.h>

static bool near(f32 a, f32 b) { return fabsf(a - b) < 0.01f; }

static void test_area(void) {
    Board b;
    board_init(&b, 9, 9);
    f32 own[81];
    // Columns 0-3 Black (secure), column 4 open, 5-7 White (secure), column 8 White likely (0.5).
    for (i32 y = 0; y < 9; ++y) {
        for (i32 x = 0; x < 9; ++x) own[y * 9 + x] = x < 4 ? 1.0f : x == 4 ? 0.1f : x < 8 ? -0.9f : -0.5f;
    }
    // Area: Black 36 + 9*0.1, White 27*0.9 + 9*0.5 + komi; lead chosen to match: no adjustment.
    f32 black = 36 + 0.9f, white = 27 * 0.9f + 4.5f + 7.0f;
    TerritoryEstimate e = territory_estimate(&b, own, 7.0f, black - white, false);
    CHECK(near(e.black, black) && near(e.white, white));
    CHECK(near(e.black_secure, 36) && near(e.white_secure, 27 + 7) && near(e.white_likely, 9) && near(e.unclaimed, 9));
    CHECK(near(e.decided, 63.0f / 81.0f));
    // KataGo says Black leads by 5: the estimate follows (to Black's secure points).
    e = territory_estimate(&b, own, 7.0f, 5.0f, false);
    CHECK(near(e.black - e.white, 5.0f) && near(e.white, white));
}

static void test_territory(void) {
    Board b;
    board_init(&b, 9, 9);
    f32 own[81];
    for (i32 i = 0; i < 81; ++i) own[i] = i < 40 ? 1.0f : -1.0f;
    b.cells[0] = STONE_BLACK;    // own stone: not territory
    b.cells[1] = STONE_WHITE;    // dead white stone in Black's area: 2 points
    b.cells[80] = STONE_WHITE;   // own stone of White
    TerritoryEstimate e = territory_estimate(&b, own, 6.5f, 0.0f, true);
    // Black: 40 - 1 (own) + 1 (prisoner) = 40 before the adjustment; White: 41 - 1 + 6.5.
    CHECK(near(e.black - e.white, 0.0f));   // adjusted to the lead
    CHECK(near(e.unclaimed, 2));
    TerritoryEstimate raw = territory_estimate(&b, own, 6.5f, 40.0f - 46.5f, true);
    CHECK(near(raw.black, 40) && near(raw.white, 46.5f));
}

static void test_human_winrate(void) {
    // Even score: 50%; symmetric; more lead, more win; stronger players convert better.
    CHECK(near(human_black_winrate(0, 100, 1), 0.5f));
    CHECK(near(human_black_winrate(5, 100, 1) + human_black_winrate(-5, 100, 1), 1.0f));
    CHECK(human_black_winrate(10, 100, 1) > human_black_winrate(5, 100, 1));
    CHECK(human_black_winrate(5, 100, 9) > human_black_winrate(5, 100, -11));
    CHECK(human_black_winrate(5, 200, 1) > human_black_winrate(5, 50, 1));   // a lead is safer late
    // The paper's anchor (professionals, 10-point lead): ~84% at move 50, ~97% at move 200;
    // 9 dan Fox is the closest level of the fit.
    f32 early = human_black_winrate(10, 50, 9), late = human_black_winrate(10, 200, 9);
    CHECK(early > 0.75f && early < 0.92f);
    CHECK(late > 0.9f && late <= 1.0f);
    CHECK(human_level_for_kyu(12) == -11 && human_level_for_kyu(1) == 0 && human_level_for_dan(1) == 1 && human_level_for_dan(9) == 9);
}

void register_estimate_tests(void) {
    test_register("estimate/human_winrate", test_human_winrate);
    test_register("estimate/area", test_area);
    test_register("estimate/territory", test_territory);
}
