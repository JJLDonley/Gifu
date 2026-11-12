#include "test.h"

#include "core/game_report.h"
#include "core/sgf.h"
#include "engine/engine.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

static const char SAMPLE[] =
    "(;GM[1]FF[4]SZ[19]KM[6.5]PB[黑棋]PW[White \\] Player]C[root comment\\\nsoft break]"
    ";B[pd];W[dd](;B[pp]C[main];W[dp])(;B[dp];W[pp]))";

static void test_parse_basic(void) {
    SgfError err;
    GameTree *t = sgf_parse(sv(SAMPLE), &err);
    CHECK(t != NULL);
    if (!t) return;
    CHECK_EQ(game_tree_width(t), 19);
    CHECK_NEAR(game_tree_komi(t), 6.5, 1e-9);
    CHECK_SV(game_tree_info(t, SV("PB")), SV("黑棋"));
    CHECK_SV(game_tree_info(t, SV("PW")), SV("White ] Player"));
    CHECK_SV(game_tree_info(t, SV("C")), SV("root commentsoft break"));

    game_tree_forward(t);   // B pd
    CHECK(t->current->has_move && t->current->move.color == STONE_BLACK);
    CHECK_EQ(t->current->move.point.x, 15);
    game_tree_forward(t);   // W dd
    CHECK_EQ(game_node_child_count(t->current), 2);
    game_tree_forward(t);   // B pp (main)
    CHECK_SV(game_node_prop(t->current, SV("C")), SV("main"));
    CHECK(game_tree_next_variation(t));   // B dp
    CHECK_EQ(t->current->move.point.x, 3);
    CHECK_EQ(t->current->move_number, 3);
    CHECK(!game_tree_next_variation(t));
    game_tree_free(t);
}

static void test_round_trip(void) {
    SgfError err;
    GameTree *t = sgf_parse(sv(SAMPLE), &err);
    CHECK(t != NULL);
    if (!t) return;
    String a = sgf_write(t);
    GameTree *t2 = sgf_parse(string_sv(&a), &err);
    CHECK(t2 != NULL);
    if (t2) {
        String b = sgf_write(t2);
        CHECK_SV(string_sv(&a), string_sv(&b));
        CHECK_SV(game_tree_info(t2, SV("PW")), SV("White ] Player"));
        string_free(&b);
        game_tree_free(t2);
    }
    string_free(&a);
    game_tree_free(t);
}

static void test_setup_and_handicap(void) {
    SgfError err;
    GameTree *t = sgf_parse(SV("(;SZ[9]HA[2]AB[cc][gg];W[cg])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    CHECK_EQ(game_tree_width(t), 9);
    CHECK_EQ(t->root->to_play, STONE_WHITE);
    CHECK_EQ(board_stone_count(&t->root->board, STONE_BLACK), 2);
    game_tree_forward(t);
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    position_spec_from_node(t, t->current, SV("chinese"), spec);
    CHECK_EQ(spec->setup_count, 2);
    CHECK_EQ(spec->move_count, 1);
    CHECK_EQ(spec->to_play, STONE_BLACK);
    free(spec);
    game_tree_free(t);
}

static void test_compressed_points(void) {
    SgfError err;
    GameTree *t = sgf_parse(SV("(;SZ[19]AB[aa:cc])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    CHECK_EQ(board_stone_count(&t->root->board, STONE_BLACK), 9);
    game_tree_free(t);
}

static void test_errors(void) {
    SgfError err;
    CHECK(sgf_parse(SV("not an sgf"), &err) == NULL);
    CHECK(sgf_parse(SV("(;B[pd]"), &err) == NULL);
    CHECK(sgf_parse(SV("(;C[unterminated)"), &err) == NULL);
}

static void test_play_and_variations(void) {
    GameTree *t = game_tree_new(19, 19, 7.5);
    Point q16 = { 15, 3 }, d4 = { 3, 15 };
    CHECK_EQ(game_tree_play(t, (Move){ STONE_BLACK, q16 }), MOVE_OK);
    CHECK_EQ(game_tree_play(t, (Move){ STONE_WHITE, d4 }), MOVE_OK);
    game_tree_back(t);
    CHECK_EQ(game_tree_play(t, (Move){ STONE_WHITE, d4 }), MOVE_OK);   // reuses child
    CHECK_EQ(game_node_child_count(t->current->parent), 1);
    game_tree_back(t);
    CHECK_EQ(game_tree_play(t, (Move){ STONE_WHITE, q16 }), MOVE_ERR_OCCUPIED);
    CHECK_EQ(game_tree_play(t, (Move){ STONE_WHITE, (Point){ 2, 2 } }), MOVE_OK);
    CHECK_EQ(game_node_child_count(t->current->parent), 2);
    GameNode *line[16];
    CHECK_EQ(game_tree_active_line(t, line, 16), 3);
    game_tree_free(t);
}

static void test_new_game(void) {
    NewGameSpec spec = { .size = 19, .komi = 0.5, .handicap = 4, .black = SV("黑"), .white = SV("White"), .rules = SV("japanese") };
    GameTree *t = game_tree_new_game(&spec);
    CHECK_EQ(board_stone_count(&t->root->board, STONE_BLACK), 4);
    CHECK_EQ(t->root->to_play, STONE_WHITE);
    CHECK_SV(game_tree_info(t, SV("HA")), SV("4"));
    CHECK_SV(game_tree_info(t, SV("PB")), SV("黑"));
    CHECK_NEAR(game_tree_komi(t), 0.5, 1e-9);
    String sgf = sgf_write(t);
    CHECK(sv_find(string_sv(&sgf), SV("AB[pd][dp][pp][dd]")) >= 0);
    string_free(&sgf);
    game_tree_free(t);
}

static void test_move_loss(void) {
    GameTree *t = game_tree_new(19, 19, 7.5);
    game_tree_play(t, (Move){ STONE_BLACK, (Point){ 3, 3 } });
    GameNode *b1 = t->current;
    game_tree_play(t, (Move){ STONE_WHITE, (Point){ 15, 15 } });
    GameNode *w1 = t->current;
    f32 loss = 0;
    CHECK(!game_node_winrate_loss(b1, &loss));   // no evals yet
    t->root->eval = (NodeEval){ true, 0.45f, -1.0f, 100 };
    b1->eval = (NodeEval){ true, 0.30f, -4.0f, 100 };   // black blundered
    w1->eval = (NodeEval){ true, 0.40f, -2.0f, 100 };   // white gave some back
    CHECK(game_node_winrate_loss(b1, &loss));
    CHECK_NEAR(loss, 0.15f, 1e-5f);
    CHECK(game_node_winrate_loss(w1, &loss));
    CHECK_NEAR(loss, 0.10f, 1e-5f);
    CHECK(game_node_score_loss(b1, &loss));
    CHECK_NEAR(loss, 3.0f, 1e-5f);
    game_tree_free(t);
}

static void test_legacy_encoding(void) {
    SgfError err;
    // PB[黑棋] in GB2312 with CA[gb2312], and a Big5 file without CA (falls back to GB18030 -> still parses).
    GameTree *t = sgf_parse_bytes(SV("(;CA[gb2312]SZ[19]PB[\xBA\xDA\xC6\xE5];B[pd])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    CHECK_SV(game_tree_info(t, SV("PB")), SV("黑棋"));
    CHECK_SV(game_tree_info(t, SV("CA")), SV("UTF-8"));
    game_tree_free(t);
    t = sgf_parse_bytes(SV("(;CA[Big5]PW[\xB6\xC2\xB4\xD1])"), &err);
    CHECK(t && sv_eq(game_tree_info(t, SV("PW")), SV("黑棋")));
    game_tree_free(t);
}

static void test_eval_persistence_and_report(void) {
    GameTree *t = game_tree_new(19, 19, 7.5);
    Point d4 = { 3, 15 }, q16 = { 15, 3 }, c3 = { 2, 16 };
    game_tree_play(t, (Move){ STONE_BLACK, q16 });
    game_tree_play(t, (Move){ STONE_WHITE, c3 });   // a bad move
    GameNode *root = t->root, *b1 = root->first_child, *w1 = b1->first_child;
    NodeEval e0 = { true, 0.50f, 0.5f, 100, 2, { q16, d4 } };
    NodeEval e1 = { true, 0.49f, 0.3f, 100, 1, { d4 } };   // black played the top move
    NodeEval e2 = { true, 0.70f, 6.0f, 100, 0, { { 0 } } }; // white lost 21%
    game_node_store_eval(t, root, &e0);
    game_node_store_eval(t, b1, &e1);
    game_node_store_eval(t, w1, &e2);

    // Round-trip through SGF text.
    String sgf = sgf_write(t);
    SgfError err;
    GameTree *u = sgf_parse(string_sv(&sgf), &err);
    CHECK(u != NULL);
    if (u) {
        GameNode *ub1 = u->root->first_child;
        CHECK(u->root->eval.valid);
        CHECK_NEAR(u->root->eval.black_winrate, 0.5f, 1e-4f);
        CHECK_EQ(u->root->eval.top_count, 2);
        CHECK(point_eq(u->root->eval.top[0], q16));
        CHECK_NEAR(ub1->eval.black_score, 0.3f, 1e-3f);

        GameNode *line[8];
        isize n = game_tree_active_line(u, line, 8);
        game_tree_to_end(u);
        n = game_tree_active_line(u, line, 8);
        GameReport report;
        game_report_compute(line, n, &report);
        CHECK_EQ(report.total_moves, 2);
        CHECK_EQ(report.player[STONE_BLACK].moves, 1);
        CHECK_EQ(report.player[STONE_BLACK].top1, 1);
        CHECK_EQ(report.player[STONE_WHITE].blunders, 1);
        CHECK_EQ(report.player[STONE_WHITE].worst_count, 1);
        CHECK_NEAR(report.player[STONE_WHITE].worst_loss[0], 0.21f, 1e-3f);
        CHECK_NEAR(player_accuracy(&report.player[STONE_BLACK]), 1.0f, 1e-6f);
        // Deep report: no candidate visits in this analysis, so no Hawk Eye accuracy.
        CHECK_EQ(report.player[STONE_BLACK].match_moves, 0);
        CHECK_EQ(report.player[STONE_WHITE].stage[GAME_PHASE_OPENING].moves, 1);
        CHECK_EQ(report.player[STONE_WHITE].score_hist[3], 1);     // 5..10 points
        CHECK_EQ(report.player[STONE_WHITE].winrate_hist[4], 1);   // >= 20%
        CHECK_EQ(report.player[STONE_BLACK].score_hist[0], 1);
        game_tree_free(u);
    }
    string_free(&sgf);
    game_tree_free(t);
}

static void test_fox_komi(void) {
    CHECK_NEAR(sgf_normalize_komi(375), 7.5, 1e-9);    // 3.75 zi
    CHECK_NEAR(sgf_normalize_komi(325), 6.5, 1e-9);    // 3.25 zi
    CHECK_NEAR(sgf_normalize_komi(650), 6.5, 1e-9);    // Japanese 6.5 x100
    CHECK_NEAR(sgf_normalize_komi(-375), -7.5, 1e-9);
    CHECK_NEAR(sgf_normalize_komi(7.5), 7.5, 1e-9);
    CHECK_NEAR(sgf_normalize_komi(0), 0, 1e-9);
    CHECK_NEAR(sgf_normalize_komi(50), 50, 1e-9);       // big reverse komi is left alone
    SgfError err;
    GameTree *t = sgf_parse(SV("(;GM[1]SZ[19]KM[375];B[pd])"), &err);
    CHECK(t && t->komi_fixed);
    CHECK_NEAR(game_tree_komi(t), 7.5, 1e-9);
    CHECK_SV(game_node_prop(t->root, SV("KM")), SV("7.5"));
    game_tree_free(t);
    t = sgf_parse(SV("(;GM[1]SZ[19]KM[6.5];B[pd])"), &err);
    CHECK(t && !t->komi_fixed);
    game_tree_free(t);
}

static void test_phases_accuracy(void) {
    CHECK_EQ(game_phase(50, 361), GAME_PHASE_OPENING);
    CHECK_EQ(game_phase(51, 361), GAME_PHASE_MIDDLE);
    CHECK_EQ(game_phase(151, 361), GAME_PHASE_ENDGAME);
}

// Hawk Eye (LizzieYzy): accuracy = played visits / best visits (1 for the first
// choice, 0 off the list); match = top N with at least P% of the best's visits.
static void test_hawk_eye(void) {
    GameTree *t = game_tree_new(19, 19, 6.5);
    Point q16 = { 15, 3 }, d4 = { 3, 15 }, c3 = { 2, 16 }, k10 = { 9, 9 };
    NodeEval e = { .valid = true, .black_winrate = 0.5f, .top_count = 3, .top = { q16, d4, c3 }, .top_share = { 0.5f, 0.3f, 0.05f } };
    game_node_update_eval(t, t->root, &e);
    CHECK(game_tree_play(t, (Move){ STONE_BLACK, d4 }) == MOVE_OK);
    GameNode *b1 = t->current;
    f32 ratio = 0, acc = 0;
    i32 rank = 0;
    CHECK(node_match_value(b1, &ratio, &rank) && rank == 2 && fabsf(ratio - 0.6f) < 1e-5f);
    CHECK(node_accuracy(b1, &acc) && fabsf(acc - 60.0f) < 1e-3f);
    CHECK(node_matches_ai(b1));                                   // 2nd of 3, 60% >= 20%
    match_rule_set((MatchRule){ 1, 20.0f });
    CHECK(!node_matches_ai(b1));                                  // only the first choice matches
    match_rule_set((MatchRule){ 3, 20.0f });
    // A move off the list: accuracy 0, no match; the first choice: 100.
    game_tree_goto(t, t->root);
    CHECK(game_tree_play(t, (Move){ STONE_BLACK, k10 }) == MOVE_OK);
    CHECK(node_accuracy(t->current, &acc) && acc == 0.0f && !node_matches_ai(t->current));
    game_tree_goto(t, t->root);
    CHECK(game_tree_play(t, (Move){ STONE_BLACK, q16 }) == MOVE_OK);
    CHECK(node_accuracy(t->current, &acc) && acc == 100.0f);
    // A third-choice move with 10% of the best's visits does not match at 20%.
    game_tree_goto(t, t->root);
    CHECK(game_tree_play(t, (Move){ STONE_BLACK, c3 }) == MOVE_OK);
    CHECK(node_match_value(t->current, &ratio, &rank) && rank == 3 && !node_matches_ai(t->current));
    // The report averages over moves with known visits.
    NodeEval after = { .valid = true, .black_winrate = 0.5f };
    game_node_update_eval(t, b1, &after);
    GameNode *line[2] = { t->root, b1 };
    GameReport r;
    game_report_compute(line, 2, &r);
    CHECK(r.player[STONE_BLACK].match_moves == 1 && r.player[STONE_BLACK].matches == 1);
    CHECK(fabsf(r.player[STONE_BLACK].accuracy - 60.0f) < 1e-3f);
    game_tree_free(t);
}

static void test_eval_shares(void) {
    // Shares round-trip through CGE[]; older CGE without shares still reads.
    GameTree *t = game_tree_new(19, 19, 7.5);
    Point q16 = { 15, 3 }, d4 = { 3, 15 }, c3 = { 2, 16 };
    game_tree_play(t, (Move){ STONE_BLACK, d4 });    // played the second choice
    game_tree_play(t, (Move){ STONE_WHITE, c3 });    // not a candidate at all
    NodeEval e0 = { .valid = true, .black_winrate = 0.5f, .visits = 1000, .top_count = 2, .top = { q16, d4 }, .top_share = { 0.6f, 0.25f } };
    NodeEval e1 = { .valid = true, .black_winrate = 0.5f, .visits = 1000, .top_count = 1, .top = { q16 }, .top_share = { 0.9f } };
    NodeEval e2 = { .valid = true, .black_winrate = 0.6f, .visits = 1000 };
    game_node_store_eval(t, t->root, &e0);
    game_node_store_eval(t, t->root->first_child, &e1);
    game_node_store_eval(t, t->root->first_child->first_child, &e2);
    String sgf = sgf_write(t);
    CHECK(sv_find(string_sv(&sgf), SV("pd:0.600 dp:0.250")) > 0);
    SgfError err;
    GameTree *u = sgf_parse(string_sv(&sgf), &err);
    CHECK(u != NULL);
    CHECK_NEAR(u->root->eval.top_share[1], 0.25f, 1e-4f);
    GameNode *line[8];
    game_tree_to_end(u);
    isize n = game_tree_active_line(u, line, 8);
    GameReport r;
    game_report_compute(line, n, &r);
    CHECK_EQ(r.player[STONE_BLACK].agreement_moves, 1);
    CHECK_NEAR(r.player[STONE_BLACK].agreement, 25.0f, 1e-3f);
    CHECK_EQ(r.player[STONE_WHITE].agreement_moves, 1);
    CHECK_NEAR(r.player[STONE_WHITE].agreement, 0.0f, 1e-3f);
    string_free(&sgf);
    game_tree_free(u);
    game_tree_free(t);
    // Old format: no shares, agreement unknown.
    t = sgf_parse(SV("(;SZ[19]CGE[0.5,0.0,100,pd dd];B[pd]CGE[0.5,0.0,100,dd])"), &err);
    CHECK(t && t->root->eval.top_count == 2 && t->root->eval.top_share[0] == 0.0f);
    f32 share = 1;
    CHECK(!node_played_share(t->root->first_child, &share));
    game_tree_free(t);
}

static void test_tree_revision(void) {
    GameTree *t = game_tree_new(19, 19, 7.5);
    u64 r0 = t->revision;
    game_tree_play(t, (Move){ STONE_BLACK, { 3, 3 } });
    CHECK(t->revision > r0);
    u64 r1 = t->revision;
    game_node_set_prop(t, t->current, SV("C"), SV("note"));
    CHECK(t->revision > r1);
    u64 r2 = t->revision;
    game_node_remove_prop(t, t->current, SV("C"));
    CHECK(t->revision > r2);
    // Deeper evaluations win: a shallow live result does not replace a game analysis.
    NodeEval deep = { .valid = true, .black_winrate = 0.6f, .visits = 500 };
    NodeEval shallow = { .valid = true, .black_winrate = 0.3f, .visits = 20 };
    CHECK(game_node_update_eval(t, t->current, &deep));
    u64 r3 = t->revision;
    CHECK(!game_node_update_eval(t, t->current, &shallow));
    CHECK_EQ(t->revision, r3);
    CHECK_NEAR(t->current->eval.black_winrate, 0.6f, 1e-6f);
    NodeEval deeper = { .valid = true, .black_winrate = 0.55f, .visits = 900 };
    CHECK(game_node_update_eval(t, t->current, &deeper));
    CHECK_EQ(t->current->eval.visits, 900);
    game_tree_free(t);
}

// The Distribution limits are user-editable; they also define mistakes and blunders.
static void test_loss_limits(void) {
    LossLimits l;
    loss_limits_parse(SV("3, 6, 12, 24"), SV("1,2,4,8"), &l);
    CHECK(fabsf(l.winrate[0] - 0.03f) < 1e-6f && fabsf(l.winrate[3] - 0.24f) < 1e-6f && l.score[2] == 4.0f);
    loss_limits_parse(SV("5,3,10,20"), SV("1,2"), &l);   // not increasing / too short: the defaults
    LossLimits d = loss_limits_default();
    CHECK(memcmp(&l, &d, sizeof(l)) == 0);
    loss_limits_parse(SV("3,6,12,24"), SV(""), &l);
    loss_limits_set(&l);
    CHECK(!loss_is_mistake(0.05f) && loss_is_mistake(0.06f) && !loss_is_mistake(0.12f) && loss_is_blunder(0.12f));
    String text = loss_limits_format(loss_limits()->winrate, 100.0f);
    CHECK_SV(string_sv(&text), SV("3,6,12,24"));
    string_free(&text);
    loss_limits_set(&d);   // other tests use the defaults
}

void register_sgf_tests(void) {
    test_register("tree/revision", test_tree_revision);
    test_register("tree/eval_shares", test_eval_shares);
    test_register("report/phases_accuracy", test_phases_accuracy);
    test_register("report/loss_limits", test_loss_limits);
    test_register("report/hawk_eye", test_hawk_eye);
    test_register("sgf/fox_komi", test_fox_komi);
    test_register("tree/eval_report", test_eval_persistence_and_report);
    test_register("sgf/legacy_encoding", test_legacy_encoding);
    test_register("tree/move_loss", test_move_loss);
    test_register("sgf/new_game", test_new_game);
    test_register("sgf/parse_basic", test_parse_basic);
    test_register("sgf/round_trip", test_round_trip);
    test_register("sgf/setup_handicap", test_setup_and_handicap);
    test_register("sgf/compressed_points", test_compressed_points);
    test_register("sgf/errors", test_errors);
    test_register("tree/play_variations", test_play_and_variations);
}
