#include "test.h"

#include "core/profile.h"

#include <math.h>

static bool near(f32 a, f32 b) { return fabsf(a - b) < 0.001f; }

static void test_totals(void) {
    GameReport g1 = { 0 }, g2 = { 0 };
    g1.player[STONE_BLACK] = (PlayerReport){ .moves = 100, .top1 = 50, .accuracy = 80, .avg_score_loss = 1.0f, .score_moves = 100, .agreement = 40, .agreement_moves = 100,
                                             .mistakes = 3, .score_hist = { 60, 20, 10, 5, 5 } };
    g1.player[STONE_BLACK].stage[GAME_PHASE_OPENING] = (StageReport){ .moves = 25, .accuracy = 90 };
    g1.player[STONE_WHITE] = (PlayerReport){ .moves = 99, .accuracy = 70 };   // the opponent: ignored
    g2.player[STONE_BLACK] = (PlayerReport){ .moves = 300, .top1 = 120, .accuracy = 60, .avg_score_loss = 2.0f, .score_moves = 300, .agreement = 20, .agreement_moves = 100,
                                             .mistakes = 7, .score_hist = { 100, 100, 50, 30, 20 } };
    g2.player[STONE_BLACK].stage[GAME_PHASE_OPENING] = (StageReport){ .moves = 75, .accuracy = 70 };
    ProfileTotals t = { 0 };
    profile_add_game(&t, &g1, STONE_BLACK, PROFILE_RESULT_WIN);
    profile_add_game(&t, &g2, STONE_BLACK, PROFILE_RESULT_LOSS);
    profile_add_game(&t, &g1, STONE_EMPTY, PROFILE_RESULT_WIN);   // not a colour: ignored
    const PlayerReport *b = &t.report.player[STONE_BLACK];
    CHECK(t.games[STONE_BLACK] == 2 && t.wins[STONE_BLACK] == 1 && t.losses[STONE_BLACK] == 1 && t.games[STONE_WHITE] == 0);
    CHECK(b->moves == 400 && b->top1 == 170 && b->mistakes == 10);
    CHECK(near(b->accuracy, 65.0f));         // (80*100 + 60*300) / 400
    CHECK(near(b->avg_score_loss, 1.75f) && b->score_moves == 400);   // weighted by the moves with scores
    CHECK(near(b->agreement, 30.0f));         // weighted by the moves with shares
    CHECK(near(b->stage[GAME_PHASE_OPENING].accuracy, 75.0f));
    CHECK(b->score_hist[0] == 160 && b->score_hist[4] == 25);
    CHECK(t.report.player[STONE_WHITE].moves == 0);
    GameReport white = { 0 };
    white.player[STONE_WHITE] = (PlayerReport){ .moves = 100, .accuracy = 90 };
    profile_add_game(&t, &white, STONE_WHITE, PROFILE_RESULT_UNKNOWN);
    PlayerReport all;
    profile_combined(&t, &all);
    CHECK(all.moves == 500 && near(all.accuracy, (65.0f * 400 + 90.0f * 100) / 500));
}

static void test_names_results(void) {
    CHECK(profile_name_matches(SV("J"), SV("j, Jeremiah ,jjl")));
    CHECK(profile_name_matches(SV(" jjl "), SV("j, Jeremiah ,JJL")));
    CHECK(!profile_name_matches(SV("Eric"), SV("j, Jeremiah")));
    CHECK(!profile_name_matches(SV(""), SV("j")) && !profile_name_matches(SV("J"), SV("")));
    CHECK(!profile_name_matches(SV("Jer"), SV("Jeremiah")));   // whole names only
    CHECK(profile_result(SV("B+3.5"), STONE_BLACK) == PROFILE_RESULT_WIN);
    CHECK(profile_result(SV("B+R"), STONE_WHITE) == PROFILE_RESULT_LOSS);
    CHECK(profile_result(SV("w+T"), STONE_WHITE) == PROFILE_RESULT_WIN);
    CHECK(profile_result(SV("0"), STONE_BLACK) == PROFILE_RESULT_UNKNOWN && profile_result(SV("Void"), STONE_BLACK) == PROFILE_RESULT_UNKNOWN);
}

void register_profile_tests(void) {
    test_register("profile/totals", test_totals);
    test_register("profile/names_results", test_names_results);
}
