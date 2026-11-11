#include "test.h"

#include "core/markup.h"
#include "core/sgf.h"

#include <stdlib.h>
#include <string.h>

static i32 at(i32 x, i32 y) { return y * 9 + x; }

static void test_markup_read(void) {
    SgfError err;
    GameTree *t = sgf_parse(SV("(;SZ[9];B[ee]TR[aa]SQ[ba:ca]CR[ab]MA[bb]LB[cc:A][dd:12]AR[aa:ee]LN[ii:hh]TB[ah]TW[ia]"
                               "CGS[ff8000:fa][ff8000:ga][3070e0:fb:gc]CGP[ff0000:1.5,2.25 3,4 3.5,4.5][00ff00:1,1])"),
                           &err);
    CHECK(t != NULL);
    if (!t) return;
    game_tree_to_end(t);
    Markup m;
    markup_read(t->current, 9, 9, &m);
    CHECK_EQ(m.shape[at(0, 0)], MARK_TRIANGLE);
    CHECK(m.shape[at(1, 0)] == MARK_SQUARE && m.shape[at(2, 0)] == MARK_SQUARE);   // "ba:ca" range
    CHECK_EQ(m.shape[at(0, 1)], MARK_CIRCLE);
    CHECK_EQ(m.shape[at(1, 1)], MARK_CROSS);
    CHECK(strcmp(m.label[at(2, 2)].text, "A") == 0 && strcmp(m.label[at(3, 3)].text, "12") == 0);
    CHECK(m.arrow_count == 1 && m.arrows[0].to.x == 4 && m.arrows[0].to.y == 4);
    CHECK(m.line_count == 1 && m.lines[0].from.x == 8);
    CHECK(m.territory[at(0, 7)] == STONE_BLACK && m.territory[at(8, 0)] == STONE_WHITE);
    CHECK(m.shaded[at(5, 0)] && m.shade[at(5, 0)] == 0xff8000);
    CHECK(m.shaded[at(6, 2)] && m.shade[at(6, 2)] == 0x3070e0);   // range fb:gc (2x2)
    CHECK_EQ(m.stroke_count, 1);   // the one-point stroke is dropped
    CHECK(m.strokes && m.strokes[0].count == 3 && m.strokes[0].points[0][1] == 2.25f && m.strokes[0].rgb == 0xff0000);

    // Round trip: write, save as SGF text, parse again, read again.
    markup_write(t, t->current, &m);
    String text = sgf_write(t);
    GameTree *t2 = sgf_parse(string_sv(&text), &err);
    CHECK(t2 != NULL);
    if (t2) {
        game_tree_to_end(t2);
        Markup m2;
        markup_read(t2->current, 9, 9, &m2);
        CHECK(memcmp(m.shape, m2.shape, sizeof(m.shape)) == 0);
        CHECK(memcmp(m.label, m2.label, sizeof(m.label)) == 0);
        CHECK(memcmp(m.territory, m2.territory, sizeof(m.territory)) == 0);
        CHECK(memcmp(m.shaded, m2.shaded, sizeof(m.shaded)) == 0);
        CHECK(m2.arrow_count == 1 && m2.line_count == 1 && m2.stroke_count == 1);
        CHECK(m2.strokes && m2.strokes[0].count == 3 && m2.strokes[0].points[2][0] == 3.5f);
        markup_free(&m2);
        game_tree_free(t2);
    }
    string_free(&text);
    // Nonsense values are ignored, not fatal.
    GameTree *t3 = sgf_parse(SV("(;SZ[9]TR[zz]LB[aa]AR[aa:aa]CGS[nothex:aa]CGP[ff0000:x,y])"), &err);
    if (t3) {
        Markup m3;
        markup_read(t3->root, 9, 9, &m3);
        CHECK(markup_is_empty(&m3));
        markup_free(&m3);
        game_tree_free(t3);
    }
    markup_free(&m);
    game_tree_free(t);
}

static void test_markup_edit(void) {
    Markup m;
    markup_read(NULL, 9, 9, &m);
    CHECK(markup_is_empty(&m));
    // Segments toggle (either direction removes).
    CHECK(markup_toggle_segment(m.arrows, &m.arrow_count, (Point){ 0, 0 }, (Point){ 4, 4 }));
    CHECK(!markup_toggle_segment(m.arrows, &m.arrow_count, (Point){ 4, 4 }, (Point){ 0, 0 }));
    CHECK_EQ(m.arrow_count, 0);
    CHECK(!markup_toggle_segment(m.lines, &m.line_count, (Point){ 2, 2 }, (Point){ 2, 2 }));   // zero length
    // Next labels skip used ones.
    char next[MARKUP_MAX_LABEL];
    markup_next_label(&m, false, next);
    CHECK(strcmp(next, "A") == 0);
    strcpy(m.label[at(1, 1)].text, "A");
    strcpy(m.label[at(2, 1)].text, "C");
    markup_next_label(&m, false, next);
    CHECK(strcmp(next, "B") == 0);
    markup_next_label(&m, true, next);
    CHECK(strcmp(next, "1") == 0);
    // The eraser takes everything on / through the point.
    m.shape[at(4, 4)] = MARK_CIRCLE;
    m.shaded[at(4, 4)] = true;
    markup_toggle_segment(m.lines, &m.line_count, (Point){ 4, 4 }, (Point){ 8, 8 });
    PenStroke *s = markup_add_stroke(&m, 0xff0000);
    s->points[0][0] = 3, s->points[0][1] = 4, s->points[1][0] = 5, s->points[1][1] = 4, s->count = 2;   // passes (4,4)
    PenStroke *far = markup_add_stroke(&m, 0x00ff00);
    far->points[0][0] = 0, far->points[0][1] = 8, far->points[1][0] = 1, far->points[1][1] = 8, far->count = 2;
    markup_erase_at(&m, (Point){ 4, 4 });
    CHECK(m.shape[at(4, 4)] == MARK_NONE && !m.shaded[at(4, 4)] && m.line_count == 0);
    CHECK(m.stroke_count == 1 && m.strokes[0].rgb == 0x00ff00);
    markup_clear(&m);
    CHECK(markup_is_empty(&m) && m.width == 9);
    markup_free(&m);
}

static void test_markup_shading(void) {
    Board b;
    board_init(&b, 9, 9);
    // A wall of Black on column 2: the left region is 2 columns wide (18 points).
    for (i32 y = 0; y < 9; ++y) board_set(&b, (Point){ 2, (i8)y }, STONE_BLACK);
    bool region[BOARD_MAX_POINTS];
    CHECK_EQ(markup_empty_region(&b, (Point){ 0, 0 }, region), 18);
    CHECK(region[at(1, 8)] && !region[at(2, 0)] && !region[at(3, 0)]);
    CHECK_EQ(markup_empty_region(&b, (Point){ 2, 0 }, region), 0);   // on a stone
    Markup m;
    markup_read(NULL, 9, 9, &m);
    for (i32 y = 0; y < 9; ++y) {
        for (i32 x = 0; x < 3; ++x) m.shaded[at(x, y)] = true, m.shade[at(x, y)] = 0x111111;   // 27, 9 of them on stones
    }
    m.shaded[at(8, 8)] = true, m.shade[at(8, 8)] = 0x222222;
    ShadeCount counts[4];
    i32 n = markup_shade_counts(&m, &b, false, counts, 4);
    CHECK(n == 2 && counts[0].rgb == 0x111111 && counts[0].points == 27 && counts[1].points == 1);
    n = markup_shade_counts(&m, &b, true, counts, 4);
    CHECK(n == 2 && counts[0].points == 18);
    markup_free(&m);
}

static void test_setup_stones(void) {
    SgfError err;
    GameTree *t = sgf_parse(SV("(;SZ[9]AB[aa];C[setup here])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    game_tree_to_end(t);
    GameNode *n = t->current;
    // Place a white stone, then click it again to take it away (nothing left behind).
    setup_toggle_stone(t, n, (Point){ 1, 1 }, STONE_WHITE);
    game_tree_rebuild(t, n);
    CHECK_EQ(board_at(&n->board, (Point){ 1, 1 }), STONE_WHITE);
    CHECK_SV(game_node_prop(n, SV("AW")), SV("bb"));
    setup_toggle_stone(t, n, (Point){ 1, 1 }, STONE_WHITE);
    game_tree_rebuild(t, n);
    CHECK_EQ(board_at(&n->board, (Point){ 1, 1 }), STONE_EMPTY);
    CHECK(!game_node_find_prop(n, SV("AW")));
    // A stone from an earlier node is removed with AE, and put back by dropping the AE.
    setup_toggle_stone(t, n, (Point){ 0, 0 }, STONE_BLACK);
    game_tree_rebuild(t, n);
    CHECK_EQ(board_at(&n->board, (Point){ 0, 0 }), STONE_EMPTY);
    CHECK_SV(game_node_prop(n, SV("AE")), SV("aa"));
    setup_toggle_stone(t, n, (Point){ 0, 0 }, STONE_BLACK);
    game_tree_rebuild(t, n);
    CHECK_EQ(board_at(&n->board, (Point){ 0, 0 }), STONE_BLACK);
    CHECK(!game_node_find_prop(n, SV("AE")) && !game_node_find_prop(n, SV("AB")));
    // Other colour over an existing stone replaces it.
    setup_toggle_stone(t, n, (Point){ 0, 0 }, STONE_WHITE);
    game_tree_rebuild(t, n);
    CHECK_EQ(board_at(&n->board, (Point){ 0, 0 }), STONE_WHITE);
    game_tree_free(t);
}

void register_markup_tests(void) {
    test_register("markup/setup_stones", test_setup_stones);
    test_register("markup/read_write", test_markup_read);
    test_register("markup/edit", test_markup_edit);
    test_register("markup/shading", test_markup_shading);
}
