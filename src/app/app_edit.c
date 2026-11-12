// Editing moves in the middle of a game (core/move_edit.c) from commands, the
// board's menus and stone dragging, with undo / redo of every structural edit.
#include "app/app.h"

#include "app/commands.h"

static void push_undo(App *app, const MoveEditRecord *r) {
    vec_push(&app->undo, *r);
    vec_clear(&app->redo);
    app->main.synced_node = NULL;   // the engine follows the new line
    app_mark_edited(app);
}

bool app_move_edit(App *app, const MoveEdit *edit) {
    MoveEditRecord r;
    if (!move_edit_apply(app->tree, edit, &r)) {
        app_set_status(app, "That edit is not possible here");
        return false;
    }
    push_undo(app, &r);
    if (r.skipped) app_set_status(app, "%d later move%s became illegal and %s left out", r.skipped, r.skipped == 1 ? "" : "s", r.skipped == 1 ? "was" : "were");
    return true;
}

bool app_delete_branch(App *app, GameNode *node) {
    if (!node->parent) return false;
    MoveEditRecord r = { .parent = node->parent, .old_child = node, .current_before = app->tree->current };
    r.index = game_tree_unlink(app->tree, node);
    r.current_after = app->tree->current;
    push_undo(app, &r);
    return true;
}

void app_undo(App *app) {
    if (!app->undo.len) {
        app_set_status(app, "Nothing to undo");
        return;
    }
    MoveEditRecord r = app->undo.items[--app->undo.len];
    move_edit_undo(app->tree, &r);
    vec_push(&app->redo, r);
    app->main.synced_node = NULL;
    app_mark_edited(app);
}

void app_redo(App *app) {
    if (!app->redo.len) {
        app_set_status(app, "Nothing to redo");
        return;
    }
    MoveEditRecord r = app->redo.items[--app->redo.len];
    move_edit_redo(app->tree, &r);
    vec_push(&app->undo, r);
    app->main.synced_node = NULL;
    app_mark_edited(app);
}

void app_edit_history_clear(App *app) {
    vec_clear(&app->undo);
    vec_clear(&app->redo);
    app->moving_node = NULL;
}

void app_edit_begin_move(App *app, Point from) {
    app->moving_node = move_edit_stone_node(app->tree, from);
    if (app->moving_node) app_set_status(app, "Click where the stone should go (Esc cancels)");
    else app_set_status(app, "That stone was not played as a move (a setup stone)");
}

bool app_edit_finish_move(App *app, Point to) {
    GameNode *node = app->moving_node;
    if (!node) return false;
    app->moving_node = NULL;
    app_move_edit(app, &(MoveEdit){ MOVE_EDIT_RELOCATE, node, { STONE_EMPTY, to } });
    return true;
}

// ---- Commands (the board menus set app->edit_point first) ---------------------------------

#define APP(c) ((App *)(c))
#define CAPP(c) ((const App *)(c))

static bool point_empty(const App *app) {
    const Board *b = &app->tree->current->board;
    return board_contains(b, app->edit_point) && board_at(b, app->edit_point) == STONE_EMPTY;
}
static bool point_stone(const App *app) { return move_edit_stone_node(app->tree, app->edit_point) != NULL; }

static void insert(App *app, Stone color) {
    if (point_empty(app)) app_move_edit(app, &(MoveEdit){ MOVE_EDIT_INSERT, app->tree->current, { color, app->edit_point } });
}
static void cmd_insert_black(void *c) { insert(APP(c), STONE_BLACK); }
static void cmd_insert_white(void *c) { insert(APP(c), STONE_WHITE); }
static bool can_insert(const void *c) { return point_empty(CAPP(c)); }

static void stone_edit(App *app, MoveEditKind kind) {
    GameNode *node = move_edit_stone_node(app->tree, app->edit_point);
    if (node) app_move_edit(app, &(MoveEdit){ kind, node, { 0 } });
}
static void cmd_delete_move(void *c) { stone_edit(APP(c), MOVE_EDIT_DELETE); }
static void cmd_swap_color(void *c) { stone_edit(APP(c), MOVE_EDIT_SWAP); }
static void cmd_move_stone(void *c) { app_edit_begin_move(APP(c), APP(c)->edit_point); }
static void cmd_jump_to_move(void *c) {
    GameNode *node = move_edit_stone_node(APP(c)->tree, APP(c)->edit_point);
    if (node) app_goto(APP(c), node);
}
static bool can_stone(const void *c) { return point_stone(CAPP(c)); }

static void cmd_undo(void *c) { app_undo(APP(c)); }
static void cmd_redo(void *c) { app_redo(APP(c)); }
static bool can_undo(const void *c) { return CAPP(c)->undo.len > 0; }
static bool can_redo(const void *c) { return CAPP(c)->redo.len > 0; }

static void cmd_komi_up(void *c) { app_set_komi(APP(c), game_tree_komi(APP(c)->tree) + 0.5); }
static void cmd_komi_down(void *c) { app_set_komi(APP(c), game_tree_komi(APP(c)->tree) - 0.5); }

void app_edit_register_commands(App *app) {
    CommandDef defs[] = {
        { "edit.undo", "Undo", "Undo", cmd_undo, can_undo, NULL, app },
        { "edit.redo", "Redo", "Redo", cmd_redo, can_redo, NULL, app },
        { "edit.insert_black", "Insert Black", "Insert B", cmd_insert_black, can_insert, NULL, app },
        { "edit.insert_white", "Insert White", "Insert W", cmd_insert_white, can_insert, NULL, app },
        { "edit.jump_to_move", "Jump To", "Jump", cmd_jump_to_move, can_stone, NULL, app },
        { "edit.move_stone", "Move", "Move", cmd_move_stone, can_stone, NULL, app },
        { "edit.swap_color", "Swap Color", "Swap", cmd_swap_color, can_stone, NULL, app },
        { "edit.delete_move", "Delete", "Delete", cmd_delete_move, can_stone, NULL, app },
        { "game.komi_up", "Komi +0.5", "K+", cmd_komi_up, NULL, NULL, app },
        { "game.komi_down", "Komi -0.5", "K-", cmd_komi_down, NULL, NULL, app },
    };
    command_register_all(&app->commands, defs, countof(defs));
}
