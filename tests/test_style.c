#include "test.h"

#include <math.h>

#include "app/ai_style.h"
#include "core/style.h"

static void play(GameTree *t, Stone c, i8 x, i8 y) { CHECK(game_tree_play(t, (Move){ c, { x, y } }) == MOVE_OK); }

static void test_style(void) {
    GameTree *t = game_tree_new(19, 19, 7.5);
    play(t, STONE_BLACK, 3, 3);      // D16-ish: line 4 (high)
    play(t, STONE_WHITE, 16, 2);     // line 3 (low); far from Black's move: tenuki
    play(t, STONE_BLACK, 16, 3);     // touches White (contact), line 3 (x = 16 is 3rd from the right), local answer
    play(t, STONE_WHITE, 15, 3);     // touches Black, line 4, local
    play(t, STONE_BLACK, 9, 9);      // tengen: line 10, away (tenuki)
    play(t, STONE_WHITE, 2, 16);   // line 3, far
    enum { N = 64 };
    GameNode *line[N];
    isize n = game_tree_active_line(t, line, N);
    StyleReport r;
    style_compute(line, n, &r);
    const StyleCounts *b = &r.player[STONE_BLACK], *w = &r.player[STONE_WHITE];
    CHECK(b->moves == 3 && b->high == 2 && b->contact == 1);
    CHECK(b->answers == 2 && b->tenuki == 1);   // its first move answers nothing
    CHECK(w->moves == 3 && w->high == 1 && w->contact == 1);
    CHECK(w->answers == 3 && w->tenuki == 2);
    CHECK(style_height(b) > 0.66f && style_height(b) < 0.67f && style_tenuki(w) > 0.66f && style_tenuki(w) < 0.67f);
    CHECK(style_line((Point){ 0, 5 }, 19, 19) == 1 && style_line((Point){ 9, 9 }, 19, 19) == 10 && style_line((Point){ 2, 16 }, 19, 19) == 3);
    StyleCounts sum = { 0 };
    style_add(&sum, b), style_add(&sum, w);
    CHECK(sum.moves == 6 && sum.contact == 2);
    StyleCounts none = { 0 };
    CHECK(style_height(&none) == 0 && style_tenuki(&none) == 0);
    game_tree_free(t);
}

static void test_ai_style(void) {
    Board b;
    board_init(&b, 19, 19);
    b.cells[board_index(&b, (Point){ 10, 10 })] = STONE_WHITE;   // a White stone at the centre
    AnalysisSnapshot a = { .to_play = STONE_BLACK, .width = 19, .height = 19, .candidate_count = 5 };
    a.candidates[0] = (Candidate){ .move = { 3, 15 }, .visits = 1000, .score_lead = 2.0f };   // best: line 4
    a.candidates[1] = (Candidate){ .move = { 2, 9 }, .visits = 300, .score_lead = 1.5f };     // line 3, calm
    a.candidates[2] = (Candidate){ .move = { 10, 9 }, .visits = 200, .score_lead = 1.2f };    // contact, line 10
    a.candidates[3] = (Candidate){ .move = { 2, 2 }, .visits = 900, .score_lead = 0.5f };     // line 3 but loses 1.5: too much
    a.candidates[4] = (Candidate){ .move = { 16, 2 }, .visits = 10, .score_lead = 1.9f };     // line 3 but barely searched
    CHECK(ai_style_pick(AI_STYLE_TERRITORIAL, &a, &b) == 1);
    CHECK(ai_style_pick(AI_STYLE_FIGHTING, &a, &b) == 2);
    CHECK(ai_style_pick(AI_STYLE_INFLUENCE, &a, &b) == 2);   // line 10 beats the best move's line 4
    CHECK(ai_style_pick(AI_STYLE_CALM, &a, &b) == 0);        // no contact: KataGo's own move first
    CHECK(ai_style_pick(AI_STYLE_STRONGEST, &a, &b) == 0);
    a.candidate_count = 0;
    CHECK(ai_style_pick(AI_STYLE_TERRITORIAL, &a, &b) == -1);
    CHECK(ai_style_from(SV("fighting")) == AI_STYLE_FIGHTING && ai_style_from(SV("??")) == AI_STYLE_STRONGEST);
}

// The same measures for KataGo's candidates (weighted by their shares) in the same
// positions: the style is the difference.
static void test_style_vs_engine(void) {
    GameTree *t = game_tree_new(19, 19, 6.5);
    // Before Black's first move KataGo wants C3 (3rd line, 3/4 of the search) or K10 (high).
    NodeEval e = { .valid = true, .top_count = 2, .top = { { 2, 16 }, { 9, 9 } }, .top_share = { 0.6f, 0.2f } };
    game_node_update_eval(t, t->root, &e);
    CHECK(game_tree_play(t, (Move){ STONE_BLACK, { 9, 9 } }) == MOVE_OK);   // Black plays K10: high
    GameNode *line[2] = { t->root, t->current };
    StyleReport r;
    style_compute(line, 2, &r);
    f32 player = 0, engine = 0;
    CHECK(style_vs_engine_height(&r.player[STONE_BLACK], &player, &engine));
    CHECK(player == 1.0f && fabsf(engine - 0.25f) < 1e-5f);   // KataGo: 0.2 / (0.6 + 0.2) high
    CHECK(!style_vs_engine_height(&r.player[STONE_WHITE], &player, &engine));   // White has no analysed move
    game_tree_free(t);
}

void register_style_tests(void) {
    test_register("style/counts", test_style);
    test_register("style/vs_engine", test_style_vs_engine);
    test_register("style/ai_pick", test_ai_style);
}
