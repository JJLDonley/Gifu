#include "test.h"

#include "base/image.h"
#include "platform/os.h"
#include "core/board_reader.h"
#include "core/game_tree.h"
#include "core/sgf.h"
#include "app/board_sync.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool load_png(const char *name, RgbaImage *out) {
    String path = path_join(SV(GIFU_SOURCE_DIR), sv(name));
    String bytes = { 0 };
    const char *err = NULL;
    bool ok = os_read_file(string_sv(&path), &bytes) && png_decode(string_sv(&bytes), out, &err);
    string_free(&bytes);
    string_free(&path);
    return ok;
}

// Real screenshots of Gifu (wood theme on Linux, paper theme on Windows),
// cropped by hand around the board, showing tests/data/sample_cjk.sgf at move 30.
static void test_read_screenshots(void) {
    SgfError err;
    String sgf = path_join(SV(GIFU_SOURCE_DIR), SV("tests/data/sample_cjk.sgf"));
    GameTree *tree = sgf_load_file(string_sv(&sgf), &err);
    string_free(&sgf);
    CHECK(tree != NULL);
    if (!tree) return;
    for (i32 i = 0; i < 30; ++i) game_tree_forward(tree);
    const Board *expected = &tree->current->board;
    const char *const files[] = { "tests/data/board_wood.png", "tests/data/board_paper.png" };
    for (isize f = 0; f < countof(files); ++f) {
        RgbaImage im;
        CHECK(load_png(files[f], &im));
        if (!im.pixels) continue;
        BoardGrid grid;
        CHECK(board_grid_detect(&im, 0, &grid));
        CHECK_EQ(grid.size, 19);
        BoardReading reading;
        board_read_stones(&im, &grid, &reading);
        i32 wrong = 0;
        for (i32 i = 0; i < 19 * 19; ++i) wrong += reading.cells[i] != expected->cells[i];
        CHECK_EQ(wrong, 0);
        CHECK_EQ(reading.uncertain, 0);
        // Still visible in the same picture; gone in a blank one (window covered / not drawn yet).
        CHECK(fabsf(board_grid_strength(&im, &grid) - grid.strength) < 0.01f);
        CHECK(grid.strength > 0.3f);
        memset(im.pixels, 20, (usize)im.width * (usize)im.height * 4);
        CHECK(board_grid_strength(&im, &grid) < 0.02f);
        rgba_image_free(&im);
    }
    game_tree_free(tree);
}

// ---- Synthetic boards -------------------------------------------------------------------------------

static u32 rng_state;
static u32 rng(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void put(RgbaImage *im, i32 x, i32 y, u8 r, u8 g, u8 b) {
    if (x < 0 || y < 0 || x >= im->width || y >= im->height) return;
    u8 *p = im->pixels + ((usize)y * (usize)im->width + (usize)x) * 4;
    p[0] = r, p[1] = g, p[2] = b, p[3] = 255;
}

// A board of `size` lines with step `d`, origin (ox, oy), in a picture with a
// margin holding "coordinates" (short dark strokes), noisy wood, and stones.
typedef enum BoardLook { LOOK_WOOD, LOOK_LIGHT, LOOK_DARK } BoardLook;   // dark: light lines on a near-black board

static void render_board(RgbaImage *im, i32 size, f32 d, f32 ox, f32 oy, const u8 *cells, BoardLook look) {
    u8 br = look == LOOK_LIGHT ? 236 : look == LOOK_DARK ? 27 : 214, bg = look == LOOK_LIGHT ? 222 : look == LOOK_DARK ? 27 : 170,
       bb = look == LOOK_LIGHT ? 190 : look == LOOK_DARK ? 27 : 100;
    u8 line = look == LOOK_DARK ? 92 : 60;
    i32 grain = look == LOOK_DARK ? 2 : 6;
    for (i32 y = 0; y < im->height; ++y) {
        for (i32 x = 0; x < im->width; ++x) {
            i32 n = (i32)(rng() % (u32)(2 * grain + 1)) - grain + (i32)((f64)grain * sin(x * 0.05 + y * 0.01));   // grain
            put(im, x, y, (u8)CLAMP(br + n, 0, 255), (u8)CLAMP(bg + n, 0, 255), (u8)CLAMP(bb + n, 0, 255));
        }
    }
    for (i32 k = 0; k < size; ++k) {   // lines
        i32 p0x = (i32)lroundf(ox + (f32)k * d), p0y = (i32)lroundf(oy + (f32)k * d);
        for (i32 t = (i32)oy; t <= (i32)lroundf(oy + (f32)(size - 1) * d); ++t) put(im, p0x, t, line, line, line);
        for (i32 t = (i32)ox; t <= (i32)lroundf(ox + (f32)(size - 1) * d); ++t) put(im, t, p0y, line, line, line);
        // coordinate label: a small dark glyph-like block outside the grid
        for (i32 a = -3; a <= 3; ++a) {
            put(im, p0x + a, (i32)(oy - d * 0.7f), 40, 30, 20);
            put(im, (i32)(ox - d * 0.7f), p0y + a, 40, 30, 20);
        }
    }
    f32 r = d * 0.47f;
    for (i32 y = 0; y < size; ++y) {
        for (i32 x = 0; x < size; ++x) {
            Stone s = (Stone)cells[y * size + x];
            if (s == STONE_EMPTY) continue;
            f32 cx = ox + (f32)x * d, cy = oy + (f32)y * d;
            for (i32 py = (i32)(cy - r) - 1; py <= (i32)(cy + r) + 1; ++py) {
                for (i32 px = (i32)(cx - r) - 1; px <= (i32)(cx + r) + 1; ++px) {
                    f32 dist = hypotf((f32)px - cx, (f32)py - cy);
                    if (dist > r) continue;
                    bool edge = dist > r - 1.2f;
                    if (s == STONE_BLACK) put(im, px, py, (u8)(edge && look == LOOK_DARK ? 112 : look == LOOK_DARK ? 5 : 25), (u8)(look == LOOK_DARK ? 5 : 25), 28);
                    else if (edge) put(im, px, py, 150, 150, 150);
                    else put(im, px, py, 246, 246, 244);
                }
            }
        }
    }
}

static void test_read_synthetic(void) {
    const struct { i32 size; f32 d; BoardLook look; } cases[] = {
        { 19, 22.0f, LOOK_WOOD }, { 19, 31.5f, LOOK_LIGHT }, { 19, 17.0f, LOOK_WOOD }, { 13, 30.0f, LOOK_WOOD },
        { 9, 44.0f, LOOK_LIGHT }, { 19, 24.0f, LOOK_DARK },
    };
    for (isize c = 0; c < countof(cases); ++c) {
        rng_state = (u32)(c * 7919 + 1);
        i32 n = cases[c].size;
        f32 d = cases[c].d, ox = d * 1.3f + 3.0f, oy = d * 1.1f + 5.0f;
        RgbaImage im;
        CHECK(rgba_image_alloc(&im, (i32)(ox * 2 + d * (f32)(n - 1)), (i32)(oy * 2 + d * (f32)(n - 1))));
        u8 cells[BOARD_MAX_POINTS] = { 0 };
        for (i32 i = 0; i < n * n; ++i) {
            u32 v = rng() % 10;
            cells[i] = v < 3 ? STONE_BLACK : v < 6 ? STONE_WHITE : STONE_EMPTY;   // a crowded board
        }
        render_board(&im, n, d, ox, oy, cells, cases[c].look);
        BoardGrid grid;
        CHECK(board_grid_detect(&im, 0, &grid));
        CHECK_EQ(grid.size, n);
        CHECK(fabsf(grid.x0 - ox) < 1.5f && fabsf(grid.y0 - oy) < 1.5f);
        CHECK(fabsf(grid.step_x - d) < 0.3f);
        BoardReading reading;
        board_read_stones(&im, &grid, &reading);
        i32 wrong = 0;
        for (i32 i = 0; i < n * n; ++i) wrong += reading.cells[i] != cells[i];
        CHECK_EQ(wrong, 0);
        rgba_image_free(&im);
    }
    // Nothing that looks like a board.
    RgbaImage blank;
    CHECK(rgba_image_alloc(&blank, 200, 200));
    memset(blank.pixels, 200, 200 * 200 * 4);
    BoardGrid grid;
    CHECK(!board_grid_detect(&blank, 0, &grid));
    rgba_image_free(&blank);
}

// ---- Turning readings into moves ---------------------------------------------------------------------

static BoardReading reading_of(const Board *b) {
    BoardReading r = { .size = b->width };
    memcpy(r.cells, b->cells, (usize)(b->width * b->height));
    return r;
}

static void test_sync_plan(void) {
    SgfError err;
    // A short game on 9x9; later White captures Black's stone on the top edge (1,0).
    GameTree *t = sgf_parse(SV("(;SZ[9];B[ee];W[ab];B[ba];W[cc])"), &err);
    CHECK(t != NULL);
    if (!t) return;
    game_tree_to_end(t);   // 4 moves, Black to play
    BoardReading r = reading_of(&t->current->board);
    CHECK_EQ(board_sync_plan(t->current, &r).change, SYNC_SAME);

    // One move.
    Board b = t->current->board;
    CHECK_EQ(board_play(&b, (Move){ STONE_BLACK, { 6, 6 } }, (BoardRules){ 0 }), MOVE_OK);
    r = reading_of(&b);
    SyncPlan p = board_sync_plan(t->current, &r);
    CHECK_EQ(p.change, SYNC_MOVES);
    CHECK(p.move_count == 1 && p.moves[0].color == STONE_BLACK && p.moves[0].point.x == 6 && p.moves[0].point.y == 6);

    // A move that captures: Black's (1,0) has liberties (0,0), (2,0), (1,1); White fills
    // (2,0) and (1,1) (Black answering far away), then takes the last one.
    Board c = t->current->board;
    CHECK_EQ(board_play(&c, (Move){ STONE_BLACK, { 8, 8 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_play(&c, (Move){ STONE_WHITE, { 2, 0 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_play(&c, (Move){ STONE_BLACK, { 8, 7 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_play(&c, (Move){ STONE_WHITE, { 1, 1 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_play(&c, (Move){ STONE_BLACK, { 8, 6 } }, (BoardRules){ 0 }), MOVE_OK);
    for (i32 i = 0; i < 4; ++i) {   // replay them into the tree as moves the sync saw one by one
        const Move seq[] = { { STONE_BLACK, { 8, 8 } }, { STONE_WHITE, { 2, 0 } }, { STONE_BLACK, { 8, 7 } }, { STONE_WHITE, { 1, 1 } } };
        CHECK_EQ(game_tree_play(t, seq[i]), MOVE_OK);
    }
    CHECK_EQ(game_tree_play(t, (Move){ STONE_BLACK, { 8, 6 } }), MOVE_OK);
    Board d = t->current->board;   // White to play; (1,0) is in atari at (0,0)
    CHECK_EQ(board_play(&d, (Move){ STONE_WHITE, { 0, 0 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_at(&d, (Point){ 1, 0 }), STONE_EMPTY);   // captured
    r = reading_of(&d);
    p = board_sync_plan(t->current, &r);
    CHECK(p.change == SYNC_MOVES && p.move_count == 1 && p.moves[0].color == STONE_WHITE);

    // Two moves between two reads (other order on the board than the colours suggest).
    Board e = t->current->board;   // White to play
    CHECK_EQ(board_play(&e, (Move){ STONE_WHITE, { 4, 8 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_play(&e, (Move){ STONE_BLACK, { 0, 8 } }, (BoardRules){ 0 }), MOVE_OK);
    r = reading_of(&e);
    p = board_sync_plan(t->current, &r);
    CHECK(p.change == SYNC_MOVES && p.move_count == 2 && p.moves[0].color == STONE_WHITE && p.moves[1].color == STONE_BLACK);

    // Undo in the other program: an earlier position of the line.
    const GameNode *earlier = t->current->parent->parent;
    r = reading_of(&earlier->board);
    p = board_sync_plan(t->current, &r);
    CHECK(p.change == SYNC_BACK && p.node == earlier);

    // Unexplained: a stone vanished without a capture -> setup.
    Board f = t->current->board;
    board_set(&f, (Point){ 4, 4 }, STONE_EMPTY);
    r = reading_of(&f);
    CHECK_EQ(board_sync_plan(t->current, &r).change, SYNC_SETUP);

    // Another size.
    r = (BoardReading){ .size = 19 };
    CHECK_EQ(board_sync_plan(t->current, &r).change, SYNC_NEW_GAME);
    game_tree_free(t);
}

void register_board_reader_tests(void) {
    test_register("board_reader/screenshots", test_read_screenshots);
    test_register("board_reader/synthetic", test_read_synthetic);
    test_register("board_reader/sync_plan", test_sync_plan);
}
