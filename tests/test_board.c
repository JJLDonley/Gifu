#include "test.h"

#include <stdlib.h>

#include "core/board.h"
#include "core/tsumego_frame.h"

static Point P(StringView gtp) {
    Point p = POINT_PASS;
    bool ok = point_from_gtp(gtp, 19, 19, &p);
    UNUSED(ok);
    return p;
}

static MoveError play(Board *b, Stone c, const char *gtp) {
    return board_play(b, (Move){ c, P(sv(gtp)) }, (BoardRules){ 0 });
}

static void test_coords(void) {
    Point p;
    CHECK(point_from_gtp(SV("A1"), 19, 19, &p) && p.x == 0 && p.y == 18);
    CHECK(point_from_gtp(SV("T19"), 19, 19, &p) && p.x == 18 && p.y == 0);
    CHECK(point_from_gtp(SV("J10"), 19, 19, &p) && p.x == 8 && p.y == 9);   // no 'I'
    CHECK(!point_from_gtp(SV("Z1"), 19, 19, &p));
    char buf[8];
    point_to_gtp((Point){ 15, 3 }, 19, 19, buf);
    CHECK_SV(sv(buf), SV("Q16"));
    CHECK(point_from_sgf(SV("pd"), 19, 19, &p) && p.x == 15 && p.y == 3);
    CHECK(point_from_sgf(SV("tt"), 19, 19, &p) && point_is_pass(p));
    CHECK(point_from_sgf(SV(""), 19, 19, &p) && point_is_pass(p));
}

static void test_capture(void) {
    Board b;
    board_init(&b, 19, 19);
    // White A1 surrounded by black A2 and B1.
    CHECK_EQ(play(&b, STONE_WHITE, "A1"), MOVE_OK);
    CHECK_EQ(play(&b, STONE_BLACK, "A2"), MOVE_OK);
    CHECK_EQ(play(&b, STONE_BLACK, "B1"), MOVE_OK);
    CHECK_EQ(board_at(&b, P(SV("A1"))), STONE_EMPTY);
    CHECK_EQ(b.captures[STONE_BLACK], 1);
    CHECK_EQ(play(&b, STONE_BLACK, "B1"), MOVE_ERR_OCCUPIED);
}

static void test_suicide(void) {
    Board b;
    board_init(&b, 19, 19);
    play(&b, STONE_BLACK, "A2");
    play(&b, STONE_BLACK, "B1");
    CHECK_EQ(play(&b, STONE_WHITE, "A1"), MOVE_ERR_SUICIDE);
    CHECK_EQ(board_at(&b, P(SV("A1"))), STONE_EMPTY);
    BoardRules tt = { .allow_suicide = true };
    CHECK_EQ(board_play(&b, (Move){ STONE_WHITE, P(SV("A1")) }, tt), MOVE_OK);
    CHECK_EQ(board_at(&b, P(SV("A1"))), STONE_EMPTY);
    CHECK_EQ(b.captures[STONE_BLACK], 1);
}

static void test_ko(void) {
    Board b;
    board_init(&b, 19, 19);
    //   . B W .
    //   B W . W
    //   . B W .
    play(&b, STONE_BLACK, "B19");
    play(&b, STONE_WHITE, "C19");
    play(&b, STONE_BLACK, "A18");
    play(&b, STONE_WHITE, "D18");
    play(&b, STONE_BLACK, "B17");
    play(&b, STONE_WHITE, "C17");
    play(&b, STONE_WHITE, "B18");
    // Black captures B18 by playing C18 -> ko.
    CHECK_EQ(play(&b, STONE_BLACK, "C18"), MOVE_OK);
    CHECK_EQ(board_at(&b, P(SV("B18"))), STONE_EMPTY);
    CHECK_EQ(play(&b, STONE_WHITE, "B18"), MOVE_ERR_KO);
    // After a ko threat elsewhere it is legal again.
    play(&b, STONE_WHITE, "Q4");
    play(&b, STONE_BLACK, "Q16");
    CHECK_EQ(play(&b, STONE_WHITE, "B18"), MOVE_OK);
    CHECK_EQ(board_at(&b, P(SV("C18"))), STONE_EMPTY);
}

static void test_hash(void) {
    Board a, b;
    board_init(&a, 19, 19);
    board_init(&b, 19, 19);
    play(&a, STONE_BLACK, "D4");
    play(&a, STONE_WHITE, "Q16");
    play(&b, STONE_WHITE, "Q16");
    play(&b, STONE_BLACK, "D4");
    CHECK(a.hash == b.hash);
    CHECK(a.hash != 0);
}

static void test_star_points(void) {
    i32 count = 0;
    for (i32 y = 0; y < 19; ++y)
        for (i32 x = 0; x < 19; ++x) count += board_is_star_point(19, x, y);
    CHECK_EQ(count, 9);
    count = 0;
    for (i32 y = 0; y < 9; ++y)
        for (i32 x = 0; x < 9; ++x) count += board_is_star_point(9, x, y);
    CHECK_EQ(count, 5);
}

static void test_handicap(void) {
    Point p[9];
    CHECK_EQ(board_handicap_points(19, 19, 1, p), 0);
    CHECK_EQ(board_handicap_points(19, 19, 2, p), 2);
    CHECK(p[0].x == 15 && p[0].y == 3);   // Q16 first (GTP order)
    CHECK(p[1].x == 3 && p[1].y == 15);   // D4
    CHECK_EQ(board_handicap_points(19, 19, 5, p), 5);
    CHECK(p[4].x == 9 && p[4].y == 9);    // tengen
    CHECK_EQ(board_handicap_points(19, 19, 9, p), 9);
    CHECK_EQ(board_handicap_points(19, 19, 12, p), 9);
    CHECK_EQ(board_handicap_points(9, 9, 4, p), 4);
    CHECK(p[0].x == 6 && p[0].y == 2);    // 9x9 uses the 3-3 points
    CHECK_EQ(board_handicap_points(10, 10, 9, p), 4);   // even boards: corners only
    for (i32 i = 0; i < 9; ++i) {
        for (i32 j = i + 1; j < 9; ++j) CHECK(!point_eq(p[i], p[j]) || i >= 4);
    }
}

// ---- Tsumego frame ----

static void place(Board *b, Stone c, i32 x, i32 y) { board_set(b, (Point){ (i8)x, (i8)y }, c); }

static Board apply_frame(const Board *b, const TsumegoFrame *f) {
    Board out = *b;
    for (i32 i = 0; i < f->count; ++i) board_set(&out, f->changes[i].point, f->changes[i].color);
    return out;
}

static bool in_rect(BoardRect r, i32 x, i32 y) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; }

// Every frame group must be unconditionally alive: at least two empty points
// (outside the problem area) whose neighbours all belong to that group.
static void check_frame_alive(const Board *b, BoardRect area) {
    const i32 w = b->width, h = b->height;
    i32 group_of[BOARD_MAX_POINTS];
    for (i32 i = 0; i < w * h; ++i) group_of[i] = -1;
    i32 groups = 0;
    i32 members[BOARD_MAX_POINTS];
    for (i32 i = 0; i < w * h; ++i) {
        if (b->cells[i] == STONE_EMPTY || group_of[i] >= 0 || in_rect(area, i % w, i / w)) continue;
        i32 n = board_group(b, board_point(b, i), members, BOARD_MAX_POINTS);
        for (i32 k = 0; k < n; ++k) group_of[members[k]] = groups;
        groups++;
    }
    i32 eyes[BOARD_MAX_POINTS] = { 0 };
    for (i32 i = 0; i < w * h; ++i) {
        i32 x = i % w, y = i / w;
        if (b->cells[i] != STONE_EMPTY || in_rect(area, x, y)) continue;
        i32 g = -2;
        const i32 dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
        for (i32 k = 0; k < 4; ++k) {
            i32 nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            i32 ng = group_of[ny * w + nx];
            if (ng < 0 || (g != -2 && ng != g)) g = -1;
            else if (g == -2) g = ng;
        }
        if (g >= 0) eyes[g]++;
    }
    for (i32 g = 0; g < groups; ++g) CHECK(eyes[g] >= 2);
    CHECK(groups >= 2);
}

// Area score of the frame (outside the problem area): attacker minus defender,
// stones plus empty points whose neighbours are all one colour.
static i32 frame_area_diff(const Board *b, BoardRect area, Stone attacker) {
    i32 diff = 0;
    for (i32 y = 0; y < b->height; ++y) {
        for (i32 x = 0; x < b->width; ++x) {
            if (in_rect(area, x, y)) continue;
            Stone s = board_at(b, (Point){ (i8)x, (i8)y });
            if (s == STONE_EMPTY) {
                const i32 dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
                bool seen[3] = { false };
                for (i32 k = 0; k < 4; ++k) {
                    Point q = { (i8)(x + dx[k]), (i8)(y + dy[k]) };
                    if (board_contains(b, q)) seen[board_at(b, q)] = true;
                }
                if (seen[STONE_BLACK] != seen[STONE_WHITE] && !seen[STONE_EMPTY]) s = seen[STONE_BLACK] ? STONE_BLACK : STONE_WHITE;
            }
            if (s != STONE_EMPTY) diff += s == attacker ? 1 : -1;
        }
    }
    return diff;
}

// A placed ko threat really is one, played out on the framed board.
static void check_ko_threat(const Board *framed, const TsumegoFrame *f, Stone side) {
    const TsumegoKoThreat *t = &f->ko_threat[side];
    CHECK(t->placed);
    if (!t->placed) return;
    Stone other = stone_opponent(side);
    BoardRules rules = { 0 };
    CHECK(t->stones >= 5);
    CHECK_EQ(board_at(framed, t->move), STONE_EMPTY);
    CHECK_EQ(board_at(framed, t->answer), STONE_EMPTY);
    // Cutting first is suicide: the block still has the threat point.
    CHECK_EQ(board_check(framed, (Move){ side, t->answer }, rules), MOVE_ERR_SUICIDE);
    // The threat: the block (next to the answer point) drops to one liberty.
    Board b = *framed;
    CHECK_EQ(board_play(&b, (Move){ side, t->move }, rules), MOVE_OK);
    Point block = { -1, -1 };
    const i32 dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
    for (i32 k = 0; k < 4; ++k) {
        Point q = { (i8)(t->answer.x + dx[k]), (i8)(t->answer.y + dy[k]) };
        if (board_contains(&b, q) && board_at(&b, q) == other && board_liberties(&b, q) == 1) block = q;
    }
    CHECK(block.x >= 0);
    // Ignored: the cut captures the whole block.
    Board ignored = b;
    i32 before = ignored.captures[side];
    CHECK_EQ(board_play(&ignored, (Move){ side, t->answer }, rules), MOVE_OK);
    CHECK_EQ(ignored.captures[side] - before, t->stones);
    // Answered: the block is connected (two eyes for everything: checked once all threats are answered).
    CHECK_EQ(board_play(&b, (Move){ other, t->answer }, rules), MOVE_OK);
    CHECK(board_liberties(&b, block) > 1);
}

// Balanced: once the threats are answered, the frame's area difference
// matches the frame without threats.
static void check_frame_balance(const Board *b, const Board *framed, const TsumegoFrame *f) {
    const char *err = NULL;
    Board answered = *framed;
    for (Stone side = STONE_BLACK; side <= STONE_WHITE; ++side) {
        const TsumegoKoThreat *t = &f->ko_threat[side];
        if (!t->placed) continue;
        CHECK_EQ(board_play(&answered, (Move){ side, t->move }, (BoardRules){ 0 }), MOVE_OK);
        CHECK_EQ(board_play(&answered, (Move){ stone_opponent(side), t->answer }, (BoardRules){ 0 }), MOVE_OK);
    }
    check_frame_alive(&answered, f->area);
    TsumegoFrame plain;
    CHECK(tsumego_frame_build(b, &(TsumegoFrameOptions){ .margin = 2 }, &plain, &err));
    Board plain_board = apply_frame(b, &plain);
    i32 with = frame_area_diff(&answered, f->area, f->attacker), without = frame_area_diff(&plain_board, plain.area, plain.attacker);
    CHECK(abs(with - without) <= 4);
}

static void test_tsumego_frame_ko_threats(void) {
    const struct { i32 size; i32 x0, y0, x1, y1; } cases[] = {
        { 19, 0, 0, 3, 3 },      // corner
        { 19, 7, 0, 11, 3 },     // top side
        { 19, 7, 7, 11, 11 },    // centre
        { 13, 0, 9, 4, 12 },     // 13x13 corner
    };
    for (isize c = 0; c < countof(cases); ++c) {
        Board b;
        board_init(&b, cases[c].size, cases[c].size);
        place(&b, STONE_BLACK, cases[c].x0, cases[c].y0);
        place(&b, STONE_WHITE, cases[c].x1, cases[c].y1);
        TsumegoFrame f;
        const char *err = NULL;
        TsumegoFrameOptions o = { .margin = 2, .ko_threat_attacker = true, .ko_threat_defender = true };
        CHECK(tsumego_frame_build(&b, &o, &f, &err));
        Board framed = apply_frame(&b, &f);
        for (Stone side = STONE_BLACK; side <= STONE_WHITE; ++side) {
            if (cases[c].size == 19 || f.ko_threat[side].placed) check_ko_threat(&framed, &f, side);   // small boards: if there was room
        }
        check_frame_balance(&b, &framed, &f);
    }
    // Asked for one side only.
    Board b;
    board_init(&b, 19, 19);
    place(&b, STONE_BLACK, 2, 2);
    place(&b, STONE_WHITE, 3, 3);
    TsumegoFrame f;
    const char *err = NULL;
    CHECK(tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 2, .ko_threat_defender = true }, &f, &err));
    CHECK(!f.ko_threat[f.attacker].placed && f.ko_threat[stone_opponent(f.attacker)].placed);
    Board one = apply_frame(&b, &f);
    check_ko_threat(&one, &f, stone_opponent(f.attacker));
    check_frame_balance(&b, &one, &f);   // the split moved to make up for the block
    // No room on a small board: the frame is still made, without the threat.
    board_init(&b, 9, 9);
    place(&b, STONE_BLACK, 0, 0);
    place(&b, STONE_WHITE, 2, 2);
    CHECK(tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 2, .ko_threat_attacker = true, .ko_threat_defender = true }, &f, &err));
}

static void test_tsumego_frame_corner(void) {
    Board b;
    board_init(&b, 19, 19);
    // Black lives in the top-left corner, White surrounds.
    const i32 black[][2] = { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 2, 1 }, { 3, 1 }, { 3, 0 } };
    const i32 white[][2] = { { 0, 3 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 3, 2 }, { 4, 2 }, { 4, 1 }, { 5, 0 } };
    for (isize i = 0; i < countof(black); ++i) place(&b, STONE_BLACK, black[i][0], black[i][1]);
    for (isize i = 0; i < countof(white); ++i) place(&b, STONE_WHITE, white[i][0], white[i][1]);
    TsumegoFrame f;
    const char *err = NULL;
    CHECK(tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 2 }, &f, &err));
    CHECK_EQ(f.attacker, STONE_WHITE);
    CHECK(f.area.x0 == 0 && f.area.y0 == 0 && f.area.x1 == 7 && f.area.y1 == 5);
    Board framed = apply_frame(&b, &f);
    for (i32 y = 0; y < 19; ++y) {
        for (i32 x = 0; x < 19; ++x) {
            if (in_rect(f.area, x, y)) CHECK_EQ(board_at(&framed, (Point){ (i8)x, (i8)y }), board_at(&b, (Point){ (i8)x, (i8)y }));
        }
    }
    // The wall touches the problem area; the fill is split evenly.
    CHECK_EQ(board_at(&framed, (Point){ 8, 0 }), STONE_WHITE);
    CHECK_EQ(board_at(&framed, (Point){ 18, 18 }), STONE_BLACK);
    i32 white_n = board_stone_count(&framed, STONE_WHITE) - (i32)countof(white);
    i32 black_n = board_stone_count(&framed, STONE_BLACK) - (i32)countof(black);
    CHECK(white_n > 100 && black_n > 100 && (white_n - black_n < 20 && black_n - white_n < 20));
    check_frame_alive(&framed, f.area);

    // Forcing the attacker swaps the colours.
    CHECK(tsumego_frame_build(&b, &(TsumegoFrameOptions){ .attacker = STONE_BLACK, .margin = 2 }, &f, &err));
    framed = apply_frame(&b, &f);
    CHECK_EQ(board_at(&framed, (Point){ 8, 0 }), STONE_BLACK);
    check_frame_alive(&framed, f.area);
}

static void test_tsumego_frame_shapes(void) {
    const struct { i32 size; i32 x0, y0, x1, y1; } cases[] = {
        { 19, 7, 0, 11, 3 },     // top side
        { 19, 7, 7, 11, 11 },    // centre
        { 13, 0, 9, 4, 12 },     // bottom-left corner
        { 9, 0, 0, 2, 2 },       // small board
    };
    for (isize c = 0; c < countof(cases); ++c) {
        Board b;
        board_init(&b, cases[c].size, cases[c].size);
        place(&b, STONE_BLACK, cases[c].x0, cases[c].y0);
        place(&b, STONE_WHITE, cases[c].x1, cases[c].y1);
        TsumegoFrame f;
        const char *err = NULL;
        CHECK(tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 2 }, &f, &err));
        Board framed = apply_frame(&b, &f);
        check_frame_alive(&framed, f.area);
    }
    // A given rectangle replaces stones outside it.
    Board b;
    board_init(&b, 19, 19);
    place(&b, STONE_BLACK, 1, 1);
    place(&b, STONE_WHITE, 15, 15);
    TsumegoFrame f;
    const char *err = NULL;
    CHECK(tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 1, .has_problem = true, .problem = { 0, 0, 4, 4 } }, &f, &err));
    Board framed = apply_frame(&b, &f);
    CHECK_EQ(board_at(&framed, (Point){ 16, 15 }), stone_opponent(f.attacker));   // far area: defender
    check_frame_alive(&framed, f.area);
    // Nothing to frame.
    board_init(&b, 19, 19);
    CHECK(!tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 2 }, &f, &err));
    place(&b, STONE_BLACK, 0, 0);
    place(&b, STONE_WHITE, 18, 18);
    CHECK(!tsumego_frame_build(&b, &(TsumegoFrameOptions){ .margin = 2 }, &f, &err));
}

void register_board_tests(void) {
    test_register("board/tsumego_frame_corner", test_tsumego_frame_corner);
    test_register("board/tsumego_frame_ko_threats", test_tsumego_frame_ko_threats);
    test_register("board/tsumego_frame_shapes", test_tsumego_frame_shapes);
    test_register("board/handicap", test_handicap);
    test_register("board/coords", test_coords);
    test_register("board/capture", test_capture);
    test_register("board/suicide", test_suicide);
    test_register("board/ko", test_ko);
    test_register("board/hash", test_hash);
    test_register("board/star_points", test_star_points);
}
