#include "test.h"

#include "ui/stone_anim.h"

#include <stdlib.h>

static void test_stone_anim_move(void) {
    StoneAnim *a = calloc(1, sizeof(StoneAnim));
    Board b;
    board_init(&b, 19, 19);
    board_set(&b, (Point){ 3, 3 }, STONE_BLACK);
    stone_anim_update(a, &b, 1.0);   // first frame: snaps
    StoneLook look = stone_anim_placed(a, 3 * 19 + 3, 1.0);
    CHECK(look.scale == 1.0f && look.alpha == 1.0f);
    // A move eases in: small and transparent at first, then grows (with overshoot), then exactly 1.
    CHECK_EQ(board_play(&b, (Move){ STONE_WHITE, { 15, 15 } }, (BoardRules){ 0 }), MOVE_OK);
    stone_anim_update(a, &b, 2.0);
    i32 i = 15 * 19 + 15;
    look = stone_anim_placed(a, i, 2.0);
    CHECK_NEAR(look.scale, 0.82f, 1e-4f);
    CHECK_NEAR(look.alpha, 0.0f, 1e-4f);
    f32 last_alpha = -1, max_scale = 0;
    for (f64 t = 2.0; t <= 2.0 + STONE_IN_SECONDS; t += 0.01) {
        look = stone_anim_placed(a, i, t);
        CHECK(look.alpha >= last_alpha);   // never flickers back
        last_alpha = look.alpha, max_scale = MAX(max_scale, look.scale);
    }
    CHECK(max_scale > 1.0f && max_scale < 1.1f);   // a slight overshoot
    look = stone_anim_placed(a, i, 2.0 + STONE_IN_SECONDS);
    CHECK(look.scale == 1.0f && look.alpha == 1.0f);
    CHECK_NEAR(stone_anim_marker(a, i, 2.0), 0.0f, 1e-4f);
    CHECK_NEAR(stone_anim_marker(a, i, 2.0 + STONE_IN_SECONDS), 1.0f, 1e-4f);
    CHECK_EQ(stone_anim_placed(a, 3 * 19 + 3, 2.0).alpha, 1.0f);   // untouched stones stay put
    free(a);
}

static void test_stone_anim_capture(void) {
    StoneAnim *a = calloc(1, sizeof(StoneAnim));
    Board b;
    board_init(&b, 9, 9);
    // Black at (0,0) in atari: White's (1,0) takes it.
    board_set(&b, (Point){ 0, 0 }, STONE_BLACK);
    board_set(&b, (Point){ 0, 1 }, STONE_WHITE);
    stone_anim_update(a, &b, 1.0);
    CHECK_EQ(board_play(&b, (Move){ STONE_WHITE, { 1, 0 } }, (BoardRules){ 0 }), MOVE_OK);
    CHECK_EQ(board_at(&b, (Point){ 0, 0 }), STONE_EMPTY);
    stone_anim_update(a, &b, 1.5);
    CHECK_EQ(a->ghost_count, 1);
    CHECK(a->ghosts[0].color == STONE_BLACK && a->ghosts[0].point.x == 0 && a->ghosts[0].point.y == 0);
    StoneLook look = stone_anim_ghost(&a->ghosts[0], 1.5);
    CHECK(look.alpha == 1.0f && look.scale == 1.0f);
    look = stone_anim_ghost(&a->ghosts[0], 1.5 + STONE_OUT_SECONDS);
    CHECK_NEAR(look.alpha, 0.0f, 1e-4f);
    stone_anim_update(a, &b, 1.5 + STONE_OUT_SECONDS - 0.01);
    CHECK_EQ(a->ghost_count, 1);   // still fading
    stone_anim_update(a, &b, 1.5 + STONE_OUT_SECONDS + 0.01);
    CHECK_EQ(a->ghost_count, 0);   // done
    free(a);
}

static void test_stone_anim_jumps(void) {
    StoneAnim *a = calloc(1, sizeof(StoneAnim));
    Board b;
    board_init(&b, 19, 19);
    stone_anim_update(a, &b, 1.0);
    // A capture leaves a ghost...
    board_set(&b, (Point){ 9, 9 }, STONE_BLACK);
    stone_anim_update(a, &b, 2.0);
    board_set(&b, (Point){ 9, 9 }, STONE_EMPTY);
    stone_anim_update(a, &b, 2.05);
    CHECK_EQ(a->ghost_count, 1);
    // ... then a jump (more than STONE_ANIM_MAX_CHANGES points: a file opened, go to the end) snaps and clears it.
    for (i32 k = 0; k <= STONE_ANIM_MAX_CHANGES; ++k) board_set(&b, (Point){ (i8)k, 0 }, STONE_WHITE);
    stone_anim_update(a, &b, 2.1);
    CHECK_EQ(a->ghost_count, 0);
    CHECK_EQ(stone_anim_placed(a, 0, 2.1).alpha, 1.0f);
    // Exactly the limit still animates.
    board_init(&b, 19, 19);
    stone_anim_update(a, &b, 3.0);   // (a jump back to empty: snaps)
    for (i32 k = 0; k < STONE_ANIM_MAX_CHANGES; ++k) board_set(&b, (Point){ (i8)k, 5 }, STONE_BLACK);
    stone_anim_update(a, &b, 4.0);
    CHECK_NEAR(stone_anim_placed(a, 5 * 19, 4.0).alpha, 0.0f, 1e-4f);
    // Another board size snaps.
    Board small;
    board_init(&small, 9, 9);
    board_set(&small, (Point){ 4, 4 }, STONE_BLACK);
    stone_anim_update(a, &small, 5.0);
    CHECK_EQ(stone_anim_placed(a, 4 * 9 + 4, 5.0).alpha, 1.0f);
    free(a);
}

void register_stone_anim_tests(void) {
    test_register("stone_anim/move", test_stone_anim_move);
    test_register("stone_anim/capture", test_stone_anim_capture);
    test_register("stone_anim/jumps", test_stone_anim_jumps);
}
