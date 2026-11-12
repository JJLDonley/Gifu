#include "test.h"

#include "engine/analysis.h"
#include "engine/rank_estimator.h"
#include "core/sgf.h"
#include "engine/batch_analyzer.h"
#include "engine/game_job.h"
#include "engine/katago_tuning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char LINE[] =
    "info move Q16 visits 120 edgeVisits 120 utility 0.05 winrate 0.4812 scoreMean -0.6 scoreStdev 30.1 "
    "scoreLead -0.61 scoreSelfplay -0.9 prior 0.21 lcb 0.47 utilityLcb 0.01 weight 120 order 1 "
    "pv Q16 D4 Q4 "
    "info move D16 visits 300 edgeVisits 300 winrate 0.4900 scoreLead -0.30 prior 0.25 lcb 0.48 order 0 "
    "pv D16 Q4 pvVisits 300 120";

static void test_parse_candidates(void) {
    AnalysisSnapshot *a = calloc(1, sizeof(AnalysisSnapshot));
    a->to_play = STONE_BLACK;
    CHECK(kata_analysis_parse_line(sv(LINE), 19, 19, a));
    CHECK_EQ(a->candidate_count, 2);
    CHECK_EQ(a->total_visits, 420);
    const Candidate *best = analysis_best(a);
    CHECK_EQ(best->order, 0);
    CHECK_EQ(best->visits, 300);
    CHECK_EQ(best->move.x, 3);
    CHECK_EQ(best->move.y, 3);
    CHECK_EQ(best->pv_len, 2);
    CHECK_NEAR(best->winrate, 0.49f, 1e-5f);
    CHECK_EQ(a->candidates[1].pv_len, 3);
    CHECK_NEAR(a->candidates[1].score_lead, -0.61f, 1e-5f);
    CHECK(!a->has_ownership);
    free(a);
}

static void test_parse_ownership(void) {
    String line = string_from(SV("info move C3 visits 5 winrate 0.6 order 0 pv C3 ownership"));
    for (int i = 0; i < 9 * 9; ++i) string_pushf(&line, " %.2f", i == 0 ? 0.75 : -0.25);
    AnalysisSnapshot *a = calloc(1, sizeof(AnalysisSnapshot));
    a->to_play = STONE_WHITE;
    CHECK(kata_analysis_parse_line(string_sv(&line), 9, 9, a));
    CHECK(a->has_ownership);
    CHECK_NEAR(a->ownership[0], 0.75f, 1e-5f);
    CHECK_NEAR(a->ownership[80], -0.25f, 1e-5f);
    CHECK_EQ(a->candidates[0].pv_len, 1);
    CHECK_NEAR(analysis_black_winrate(a, 0.6f), 0.4f, 1e-5f);
    string_free(&line);
    free(a);
}

static void test_json_query(void) {
    Move setup[] = { { STONE_BLACK, { 3, 15 } } };
    Move moves[] = { { STONE_WHITE, { 15, 3 } }, { STONE_BLACK, POINT_PASS } };
    AnalysisJob job = { SV("g\"1"), 19, 19, 6.5f, SV("japanese"), setup, 1, moves, 2, STONE_WHITE, 200 };
    String q = katago_analysis_query(&job);
    CHECK_SV(string_sv(&q), SV("{\"id\":\"g\\\"1\",\"moves\":[[\"W\",\"Q16\"],[\"B\",\"pass\"]],\"initialStones\":[[\"B\",\"D4\"]],"
                               "\"rules\":\"japanese\",\"komi\":6.5,\"boardXSize\":19,\"boardYSize\":19,\"maxVisits\":200,"
                               "\"initialPlayer\":\"W\",\"analyzeTurns\":[0,1,2]}"));
    string_free(&q);
}

static void test_json_response(void) {
    TurnResult r;
    CHECK(katago_analysis_parse_response(SV("{\"id\":\"g1\",\"turnNumber\":3,\"rootInfo\":{\"winrate\":0.42,\"scoreLead\":-1.5,\"visits\":210},"
                                            "\"moveInfos\":[{\"move\":\"D16\",\"order\":1},{\"move\":\"Q4\",\"order\":0},{\"move\":\"pass\",\"order\":2}]}"),
                                         19, 19, &r));
    CHECK(r.ok);
    CHECK_EQ(r.turn, 3);
    CHECK_SV(string_sv(&r.job_id), SV("g1"));
    CHECK_NEAR(r.eval.black_winrate, 0.42f, 1e-6f);
    CHECK_NEAR(r.eval.black_score, -1.5f, 1e-6f);
    CHECK_EQ(r.eval.visits, 210);
    CHECK_EQ(r.eval.top_count, 3);
    CHECK(r.eval.top[0].x == 15 && r.eval.top[0].y == 15);   // Q4 first (order 0)
    CHECK(point_is_pass(r.eval.top[2]));
    turn_result_free(&r);
    CHECK(katago_analysis_parse_response(SV("{\"id\":\"g2\",\"error\":\"bad move\",\"field\":\"moves\"}"), 19, 19, &r));
    CHECK(!r.ok);
    CHECK_SV(string_sv(&r.error), SV("bad move"));
    turn_result_free(&r);
    CHECK(!katago_analysis_parse_response(SV("{\"id\":\"g3\",\"warning\":\"x\"}"), 19, 19, &r));
    CHECK(!katago_analysis_parse_response(SV("not json"), 19, 19, &r));
}

static void test_json_human_query(void) {
    Move moves[] = { { STONE_BLACK, { 3, 15 } }, { STONE_WHITE, { 15, 3 } } };
    i32 turns[] = { 0, 1 };
    AnalysisJob job = { .id = SV("r.r4"), .width = 19, .height = 19, .komi = 7.5f, .rules = SV("chinese"), .moves = moves,
                        .move_count = 2, .first_player = STONE_BLACK, .max_visits = 1, .turns = turns, .turn_count = 2,
                        .human_profile = SV("rank_5k"), .include_human_policy = true };
    String q = katago_analysis_query(&job);
    CHECK_SV(string_sv(&q), SV("{\"id\":\"r.r4\",\"moves\":[[\"B\",\"D4\"],[\"W\",\"Q16\"]],\"initialStones\":[],"
                               "\"rules\":\"chinese\",\"komi\":7.5,\"boardXSize\":19,\"boardYSize\":19,\"maxVisits\":1,"
                               "\"initialPlayer\":\"B\",\"analyzeTurns\":[0,1],\"includePolicy\":true,"
                               "\"overrideSettings\":{\"ignorePreRootHistory\":false,\"humanSLProfile\":\"rank_5k\"}}"));
    string_free(&q);
    // humanPolicy: width*height + pass, row-major from the top-left, -1 = illegal (3x2 board).
    TurnResult r;
    CHECK(katago_analysis_parse_response(SV("{\"id\":\"x\",\"turnNumber\":1,\"rootInfo\":{\"visits\":1},\"moveInfos\":[],"
                                            "\"humanPolicy\":[0.1,-1,0.2,0.3,0.25,0.1,0.05]}"),
                                         3, 2, &r));
    CHECK(r.ok && r.human_policy != NULL);
    if (r.human_policy) {
        CHECK_NEAR(r.human_policy[0], 0.1f, 1e-6f);
        CHECK_NEAR(r.human_policy[1], 0.0f, 1e-6f);   // illegal
        CHECK_NEAR(r.human_policy[1 * 3 + 2], 0.1f, 1e-6f);
    }
    CHECK_NEAR(r.human_pass, 0.05f, 1e-6f);
    turn_result_free(&r);
    // Wrong length (no human model, or another board size): ignored.
    CHECK(katago_analysis_parse_response(SV("{\"id\":\"x\",\"turnNumber\":0,\"rootInfo\":{},\"humanPolicy\":[0.5,0.5]}"), 3, 2, &r));
    CHECK(r.human_policy == NULL);
    turn_result_free(&r);
}

// Records what a RankBatch submits.
typedef struct FakeAnalyzer {
    BatchAnalyzer base;
    i32 submitted, cancelled, last_turn_count, last_visits;
    bool last_policy;
} FakeAnalyzer;
static bool fake_submit(BatchAnalyzer *a, const AnalysisJob *job) {
    FakeAnalyzer *f = (FakeAnalyzer *)a;
    f->submitted++;
    f->last_turn_count = job->turn_count;
    f->last_visits = job->max_visits;
    f->last_policy = job->include_human_policy && job->human_profile.len > 0;
    return true;
}
static bool fake_poll(BatchAnalyzer *a, TurnResult *out) { (void)a, (void)out; return false; }
static bool fake_alive(const BatchAnalyzer *a) { (void)a; return true; }
static void fake_cancel(BatchAnalyzer *a, StringView id) { (void)id, ((FakeAnalyzer *)a)->cancelled++; }
static void fake_destroy(BatchAnalyzer *a) { (void)a; }
static const BatchAnalyzerVTable FAKE_VTABLE = { fake_submit, fake_poll, fake_alive, fake_cancel, fake_destroy };

static void test_rank_batch(void) {
    SgfError err;
    // 3x3: B, W, B pass (not scored), W (scored), B out of turn after W? -> W W is out of turn (not scored).
    GameTree *t = sgf_parse(SV("(;SZ[3];B[aa];W[cc];B[];W[ac];W[ca])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    game_tree_to_end(t);
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    position_spec_from_node(t, t->current, SV("chinese"), spec);
    RankBatch *b = rank_batch_create(spec, SV("g0"));
    const RankProgress *p = rank_batch_progress(b);
    CHECK_EQ(p->total, 3);   // moves 0, 1, 3
    FakeAnalyzer fake = { .base.vtable = &FAKE_VTABLE };
    CHECK(rank_batch_submit(b, &fake.base));
    CHECK_EQ(fake.submitted, RANK_COUNT);
    CHECK_EQ(fake.last_turn_count, 3);
    CHECK_EQ(fake.last_visits, 1);
    CHECK(fake.last_policy);

    f32 policy[9] = { 0 };
    policy[0] = 0.5f;           // a1 in KataGo's top-left order = SGF "aa"
    policy[2 * 3 + 2] = 0.25f;  // "cc"
    policy[2 * 3 + 0] = 0.125f; // "ac"
    for (i32 r = 0; r < RANK_COUNT; ++r) {
        char id[16];
        snprintf(id, sizeof(id), "g0.r%d", r);
        for (i32 turn = 0; turn < 5; ++turn) {
            TurnResult res = { .job_id = string_from(sv(id)), .turn = turn, .ok = true, .human_policy = malloc(sizeof(policy)) };
            memcpy(res.human_policy, policy, sizeof(policy));
            CHECK(rank_batch_apply(b, &res));
            if (turn == 0) CHECK(rank_batch_apply(b, &res));   // duplicate: ignored
            turn_result_free(&res);
        }
        if (r < RANK_COUNT - 1) CHECK(!p->finished);
    }
    CHECK(p->finished && !p->failed);
    CHECK_EQ(p->done, 3);
    CHECK(p->move_scored[0] && p->move_scored[1] && !p->move_scored[2] && p->move_scored[3] && !p->move_scored[4]);
    CHECK_NEAR(p->move_p[0][0], 0.5f, 1e-6f);
    CHECK_NEAR(p->move_p[1][RANK_COUNT - 1], 0.25f, 1e-6f);
    CHECK_NEAR(p->move_p[3][5], 0.125f, 1e-6f);
    CHECK_EQ(p->tally[STONE_BLACK].moves, 1);
    CHECK_EQ(p->tally[STONE_WHITE].moves, 2);
    CHECK_EQ(rank_progress_store(p, t, t->current), 3);
    TurnResult other = { .job_id = string_from(SV("g0x.r1")), .turn = 0, .ok = true };
    CHECK(!rank_batch_apply(b, &other));
    turn_result_free(&other);
    rank_batch_cancel(b, &fake.base);   // finished: nothing to cancel
    CHECK_EQ(fake.cancelled, 0);
    rank_batch_destroy(b);

    // An error for one of the batch's jobs fails it; missing humanPolicy too.
    b = rank_batch_create(spec, SV("g1"));
    p = rank_batch_progress(b);
    TurnResult bad = { .job_id = string_from(SV("g1.r3")), .turn = 0, .ok = true };
    CHECK(rank_batch_apply(b, &bad));
    CHECK(p->finished && p->failed);
    turn_result_free(&bad);
    rank_batch_destroy(b);
    free(spec);
    game_tree_free(t);
}

static void test_game_job(void) {
    SgfError err;
    // Handicap setup at the root, a comment-only node mid-game, then moves.
    GameTree *t = sgf_parse(SV("(;SZ[9]KM[0.5]AB[cc][gg];W[cg];C[note];B[gc];W[ee])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    game_tree_to_end(t);
    GameJob job;
    game_job_build(&job, t, t->current, SV("j1"), SV("japanese"));
    CHECK_EQ(job.turns, 4);              // turns 0..3 (3 moves)
    CHECK_EQ(job.node_count, 5);         // root, W, comment, B, W
    CHECK_EQ(job.turn_of[0], 0);
    CHECK_EQ(job.turn_of[1], 1);
    CHECK_EQ(job.turn_of[2], 1);         // comment node shares the previous turn
    CHECK_EQ(job.turn_of[4], 3);
    AnalysisJob req = game_job_request(&job, 100);
    CHECK_EQ(req.setup_count, 2);
    CHECK_EQ(req.move_count, 3);
    CHECK_EQ(req.first_player, STONE_WHITE);
    TurnResult r = { .job_id = string_from(SV("j1")), .turn = 1, .ok = true, .eval = { .valid = true, .black_winrate = 0.6f } };
    CHECK(game_job_apply(&job, &r));
    CHECK(job.nodes[1]->eval.valid && job.nodes[2]->eval.valid);   // both nodes at turn 1
    CHECK(!job.nodes[3]->eval.valid);
    CHECK_EQ(job.received, 1);
    CHECK(game_job_apply(&job, &r));     // duplicate: counted once
    CHECK_EQ(job.received, 1);
    turn_result_free(&r);
    TurnResult other = { .job_id = string_from(SV("other")), .turn = 0, .ok = true };
    CHECK(!game_job_apply(&job, &other));
    turn_result_free(&other);
    CHECK(!game_job_done(&job));
    game_job_free(&job);
    game_tree_free(t);
}

// Shape of a real `kata-raw-human-nn` reply (3x2 board for brevity); occupied points are NAN.
static void test_parse_raw_nn(void) {
    static const char TEXT[] =
        "symmetry all\nwhiteWin 0.530842\nwhiteLoss 0.469124\nnoResult 0.000034\nwhiteScore 2.256\n"
        "policy\n0.100000 0.200000     NAN \n0.050000 0.600000 0.000010 \npolicyPass 0.049990 \n"
        "whiteOwnership\n0.1 0.2 0.3\n0.4 0.5 0.6\n";
    HumanPolicy h;
    CHECK(kata_raw_nn_parse(sv_from_parts(TEXT, (isize)sizeof(TEXT) - 1), 3, 2, &h));
    CHECK_NEAR(h.policy[0], 0.1f, 1e-6f);
    CHECK_NEAR(h.policy[2], 0.0f, 1e-9f);    // NAN -> 0
    CHECK_NEAR(h.policy[4], 0.6f, 1e-6f);    // row 1, column 1
    CHECK_NEAR(h.pass, 0.04999f, 1e-6f);
    CHECK_NEAR(h.white_win, 0.530842f, 1e-6f);
    CHECK(!kata_raw_nn_parse(SV("policy\n0.1 0.2\n"), 3, 2, &h));   // truncated
}

static void test_rank_tally(void) {
    RankTally t = { 0 };
    CHECK_EQ(rank_tally_best(&t), -1);
    // Moves a 3d (index 8) plays far more often than the other ranks would.
    for (i32 m = 0; m < 40; ++m) {
        f32 p[RANK_COUNT];
        for (i32 r = 0; r < RANK_COUNT; ++r) p[r] = 0.30f / (1.0f + 0.5f * (f32)abs(r - 8));
        rank_tally_add(&t, p);
    }
    CHECK_EQ(t.moves, 40);
    CHECK_EQ(rank_tally_best(&t), 8);
    CHECK_SV(rank_label(8), SV("3d"));
    CHECK_SV(rank_profile(0), SV("rank_20k"));
    f32 post[RANK_COUNT], sum = 0;
    rank_tally_posterior(&t, post);
    for (i32 r = 0; r < RANK_COUNT; ++r) sum += post[r];
    CHECK_NEAR(sum, 1.0f, 1e-4f);
    CHECK(post[8] > 0.9f);
    // A zero-probability move is floored instead of sending loglik to -inf.
    f32 zero[RANK_COUNT] = { 0 };
    rank_tally_add(&t, zero);
    CHECK_EQ(rank_tally_best(&t), 8);
}

static void test_rank_sgf(void) {
    f32 p[RANK_COUNT], q[RANK_COUNT];
    for (i32 r = 0; r < RANK_COUNT; ++r) p[r] = 0.0001234f * (f32)(r + 1) * (f32)(r + 1);
    char buffer[160];
    rank_probs_format(p, buffer, (isize)sizeof(buffer));
    CHECK(rank_probs_parse(sv(buffer), q));
    for (i32 r = 0; r < RANK_COUNT; ++r) CHECK_NEAR(q[r], p[r], p[r] * 0.01f);
    CHECK(!rank_probs_parse(SV("0.1,0.2"), q));                       // too few
    CHECK(!rank_probs_parse(SV("1,1,1,1,1,1,1,1,1,1,1,1,1"), q));     // too many
    CHECK(!rank_probs_parse(SV("x,1,1,1,1,1,1,1,1,1,1,1"), q));

    // Phases: 19x19 opening 1-50, middle 51-150, endgame 151+; scaled on 9x9.
    CHECK_EQ(rank_phase(1, 361), RANK_PHASE_OPENING);
    CHECK_EQ(rank_phase(50, 361), RANK_PHASE_OPENING);
    CHECK_EQ(rank_phase(51, 361), RANK_PHASE_MIDDLE);
    CHECK_EQ(rank_phase(151, 361), RANK_PHASE_ENDGAME);
    CHECK_EQ(rank_phase_start(RANK_PHASE_MIDDLE, 81), 12);
    CHECK_EQ(rank_phase(40, 81), RANK_PHASE_ENDGAME);

    RankSummary sum = { 0 };
    rank_summary_add(&sum, STONE_BLACK, 10, 361, p);
    rank_summary_add(&sum, STONE_BLACK, 100, 361, p);
    rank_summary_add(&sum, STONE_WHITE, 200, 361, p);
    CHECK_EQ(sum.total[STONE_BLACK].moves, 2);
    CHECK_EQ(sum.phase[RANK_PHASE_OPENING][STONE_BLACK].moves, 1);
    CHECK_EQ(sum.phase[RANK_PHASE_MIDDLE][STONE_BLACK].moves, 1);
    CHECK_EQ(sum.phase[RANK_PHASE_ENDGAME][STONE_WHITE].moves, 1);

    // The spec's moves map back to their nodes (setup nodes are not moves).
    SgfError err;
    GameTree *t = sgf_parse(SV("(;GM[1]SZ[19];B[pd];W[dp];AB[aa];W[qp];B[dd])"), &err);
    CHECK(t != NULL);
    GameNode *end = t->root;
    while (end->first_child) end = end->first_child;
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    position_spec_from_node(t, end, SV("chinese"), spec);
    const GameNode *nodes[16];
    i32 n = position_spec_move_nodes(end, nodes, 16);
    CHECK_EQ(n, spec->move_count);
    CHECK_EQ(n, 2);   // after the AB[] setup node: W[qp], B[dd]
    for (i32 i = 0; i < n; ++i) CHECK(point_eq(nodes[i]->move.point, spec->moves[i].point));
    free(spec);
    game_tree_free(t);
}

static void test_katago_tuning(void) {
    KataGoTuning t = { .search_threads = 8, .max_visits = 400, .gpu = 1, .nn_batch = 0, .nn_cache_power = 20,
                       .analysis_threads = 6, .analysis_search_threads = 3, .extra = SV(" ponderingEnabled = false , logToStderr=false") };
    String out = { 0 };
    katago_tuning_overrides(&t, false, SV("C:/katago/windows-opencl/katago.exe"), &out);
    CHECK_SV(string_sv(&out), SV(",numSearchThreads=8,maxVisits=400,openclDeviceToUse=1,nnCacheSizePowerOfTwo=20,ponderingEnabled=false,logToStderr=false"));
    string_clear(&out);
    katago_tuning_overrides(&t, true, SV("/opt/katago-cuda/katago"), &out);
    CHECK_SV(string_sv(&out), SV(",numAnalysisThreads=6,numSearchThreadsPerAnalysisThread=3,cudaDeviceToUse=1,nnCacheSizePowerOfTwo=20,ponderingEnabled=false,logToStderr=false"));
    string_clear(&out);
    // Defaults add nothing; CPU builds get no GPU key; unsafe extras are dropped.
    KataGoTuning d = { .gpu = 3, .extra = SV("a=b;rm -rf") };
    katago_tuning_overrides(&d, false, SV("katago/linux-eigenavx2/katago"), &out);
    CHECK_EQ(out.len, 0);
    katago_tuning_overrides(NULL, false, SV(""), &out);
    CHECK_EQ(out.len, 0);
    CHECK(katago_overrides_valid(SV("")));
    CHECK(katago_overrides_valid(SV("maxTime=10, numSearchThreads=4")));
    CHECK(!katago_overrides_valid(SV("maxTime")));
    CHECK(!katago_overrides_valid(SV("a=b,c=d e")));
    string_free(&out);
}

static void test_human_profile_combine(void) {
    char out[48];
    human_profile_combine(SV("rank_5k"), SV("1d"), out, (isize)sizeof(out));
    CHECK_SV(sv(out), SV("rank_5k_1d"));
    human_profile_combine(SV("preaz_3d"), SV("9d"), out, (isize)sizeof(out));
    CHECK_SV(sv(out), SV("preaz_3d_9d"));
    human_profile_combine(SV("rank_5k"), SV(""), out, (isize)sizeof(out));
    CHECK_SV(sv(out), SV("rank_5k"));
    human_profile_combine(SV("rank_5k"), SV("5k"), out, (isize)sizeof(out));   // same rank: plain profile
    CHECK_SV(sv(out), SV("rank_5k"));
    human_profile_combine(SV("proyear_1990"), SV("1d"), out, (isize)sizeof(out));   // no ranks to split
    CHECK_SV(sv(out), SV("proyear_1990"));
    CHECK(human_profile_valid(SV("rank_5k_1d")));
}

void register_analysis_tests(void) {
    test_register("analysis/human_profile_combine", test_human_profile_combine);
    test_register("analysis/katago_tuning", test_katago_tuning);
    test_register("analysis/rank_sgf", test_rank_sgf);
    test_register("analysis/rank_tally", test_rank_tally);
    test_register("analysis/raw_nn", test_parse_raw_nn);
    test_register("analysis/game_job", test_game_job);
    test_register("analysis/json_human_query", test_json_human_query);
    test_register("analysis/rank_batch", test_rank_batch);
    test_register("analysis/json_query", test_json_query);
    test_register("analysis/json_response", test_json_response);
    test_register("analysis/candidates", test_parse_candidates);
    test_register("analysis/ownership", test_parse_ownership);
}
