#include "test.h"

#include "core/move_edit.h"

#include <stdio.h>
#include <string.h>

static Move mv(Stone c, i8 x, i8 y) { return (Move){ c, { x, y } }; }

// "B3,3 W15,15 ..." of the line root..end through the current node.
static void line_text(GameTree *t, char *out, isize cap) {
    GameNode *line[64];
    isize n = game_tree_active_line(t, line, 64);
    out[0] = '\0';
    for (isize i = 0; i < n; ++i) {
        if (!line[i]->has_move) continue;
        char m[24];
        snprintf(m, sizeof(m), "%s%c%d,%d", out[0] ? " " : "", line[i]->move.color == STONE_BLACK ? 'B' : 'W', line[i]->move.point.x, line[i]->move.point.y);
        strncat(out, m, (size_t)(cap - (isize)strlen(out) - 1));
    }
}

static GameTree *sample(GameNode **nodes) {
    GameTree *t = game_tree_new(19, 19, 6.5);
    Move moves[] = { mv(STONE_BLACK, 2, 2), mv(STONE_WHITE, 16, 16), mv(STONE_BLACK, 2, 16), mv(STONE_WHITE, 16, 2) };
    for (i32 i = 0; i < 4; ++i) {
        CHECK(game_tree_play(t, moves[i]) == MOVE_OK);
        nodes[i] = t->current;
    }
    game_node_set_prop(t, nodes[2], SV("C"), SV("a comment"));
    return t;
}

static void test_edits(void) {
    GameNode *n[4];
    char text[256];
    MoveEditRecord r;

    // Delete (de-insert) White's first move: the rest stays, the view stays on the last move.
    GameTree *t = sample(n);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_DELETE, n[1] }, &r));
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B2,2 B2,16 W16,2") == 0);
    CHECK(t->current->has_move && t->current->move.point.x == 16 && t->current->move.point.y == 2);
    CHECK_SV(game_node_prop(t->current->parent, SV("C")), SV("a comment"));   // the comment came along
    move_edit_undo(t, &r);
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B2,2 W16,16 B2,16 W16,2") == 0 && t->current == n[3]);   // the original nodes back
    move_edit_redo(t, &r);
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B2,2 B2,16 W16,2") == 0);
    game_tree_free(t);

    // Swap the colour of the first move.
    t = sample(n);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_SWAP, n[0] }, &r));
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "W2,2 W16,16 B2,16 W16,2") == 0);
    game_tree_free(t);

    // Relocate (drag) a stone; viewed from the middle of the game.
    t = sample(n);
    game_tree_goto(t, n[2]);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_RELOCATE, n[2], mv(STONE_EMPTY, 9, 9) }, &r));
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B2,2 W16,16 B9,9 W16,2") == 0);
    CHECK(t->current->move.point.x == 9 && t->current->move.point.y == 9);   // still the edited move
    game_tree_free(t);

    // Insert after White's first move: the later moves follow the new one.
    t = sample(n);
    game_tree_goto(t, n[1]);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_INSERT, n[1], mv(STONE_BLACK, 9, 9) }, &r));
    game_tree_to_end(t);
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B2,2 W16,16 B9,9 B2,16 W16,2") == 0);
    CHECK(game_node_child_count(n[1]) == 1);   // replaced, not a new variation
    GameNode *inserted = n[1]->first_child;
    CHECK(inserted->to_play == STONE_BLACK);   // still Black to play after inserting a black stone
    MoveEditRecord r2;   // and it stays so when the line is replayed by a later edit
    game_tree_goto(t, n[1]->first_child->first_child);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_SWAP, n[0]->first_child }, &r2));
    CHECK(t->root->first_child->first_child->first_child->to_play == STONE_BLACK);
    game_tree_free(t);

    // Moving the first stone onto the last move's point: that move is left out.
    t = sample(n);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_RELOCATE, n[0], mv(STONE_EMPTY, 16, 2) }, &r));
    CHECK(r.skipped == 1);
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B16,2 W16,16 B2,16") == 0);
    game_tree_free(t);

    // Moving a stone onto the point of an existing variation: that variation is
    // left alone (and still there after undo).
    t = sample(n);
    game_tree_goto(t, n[0]);
    CHECK(game_tree_play(t, mv(STONE_WHITE, 9, 9)) == MOVE_OK);
    GameNode *variation = t->current;
    game_tree_goto(t, n[3]);
    CHECK(move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_RELOCATE, n[1], mv(STONE_EMPTY, 9, 9) }, &r));
    CHECK(game_node_child_count(n[0]) == 2 && n[0]->first_child != variation && n[0]->last_child == variation);
    CHECK(game_node_child_count(variation) == 0);
    move_edit_undo(t, &r);
    CHECK(n[0]->first_child == n[1] && n[0]->last_child == variation && game_node_child_count(n[0]) == 2);
    game_tree_free(t);

    // An illegal edit changes nothing.
    t = sample(n);
    CHECK(!move_edit_apply(t, &(MoveEdit){ MOVE_EDIT_RELOCATE, n[3], mv(STONE_EMPTY, 2, 2) }, &r));   // occupied
    line_text(t, text, sizeof(text));
    CHECK(strcmp(text, "B2,2 W16,16 B2,16 W16,2") == 0 && t->current == n[3]);
    // Which move put a stone there.
    CHECK(move_edit_stone_node(t, (Point){ 2, 16 }) == n[2] && move_edit_stone_node(t, (Point){ 9, 9 }) == NULL);
    game_tree_free(t);
}

void register_move_edit_tests(void) { test_register("move_edit/edits", test_edits); }
