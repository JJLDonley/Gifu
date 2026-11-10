// Board sync glue: the on-screen area picker (a helper process), the sync
// worker (board_sync.c) and turning its readings into moves on the game tree.
#include "app/app.h"

#include "base/log.h"
#include "base/os.h"
#include "engine/line_channel.h"

#include <stdio.h>
#include <stdlib.h>

static bool parse_area(StringView text, ScreenRect *out) {
    i64 v[4];
    for (i32 i = 0; i < 4; ++i) {
        if (!sv_parse_i64(sv_trim(sv_chop_char(&text, ',')), &v[i])) return false;
    }
    if (v[2] < 40 || v[3] < 40) return false;
    *out = (ScreenRect){ (i32)v[0], (i32)v[1], (i32)v[2], (i32)v[3] };
    return true;
}

bool app_sync_running(const App *app) { return app->board_sync != NULL || app->area_picker != NULL; }

void app_sync_stop(App *app) {
    if (app->area_picker) {
        line_channel_stop(app->area_picker, SV(""));
        app->area_picker = NULL;
    }
    if (!app->board_sync) return;
    board_sync_stop(app->board_sync);
    app->board_sync = NULL;
    app_set_status(app, "Board sync stopped");
}

static void start_sync(App *app, ScreenRect area) {
    app_sync_stop(app);
    BoardSyncSettings s = {
        .backend = string_sv(&app->config.sync_capture),
        .command = string_sv(&app->config.sync_capture_command),
        .area = area,
        .interval_ms = app->config.sync_interval_ms,
    };
    app->board_sync = board_sync_start(&s);
    app->board_sync_state = BOARD_SYNC_STARTING;
    app->sync_node = app->tree->current;   // continue from what the user sees now
    app->sync_tree = app->tree;
    app_set_status(app, "Board sync: looking for the board in %dx%d at %d,%d", area.width, area.height, area.x, area.y);
}

void app_sync_select(App *app) {
    app_sync_stop(app);
    ScreenRect d = app->desktop;
    if (d.width <= 0 || d.height <= 0) {
        app_set_status(app, "Board sync: the screen size is not known");
        return;
    }
    // The picker is this program again: a transparent window over every monitor.
    String exe = path_join(string_sv(&app->exe_dir), SV("catagui"));
#ifdef _WIN32
    string_push(&exe, SV(".exe"));
#endif
    char area[64];
    snprintf(area, sizeof(area), "%d,%d,%d,%d", d.x, d.y, d.width, d.height);
    StringView hint = app_tr(app, "sync.pick_hint", "Drag a box around the board  -  Esc cancels");
    StringView args[] = { SV("--select-area"), sv(area), SV("--select-hint"), hint };
    ProcessSpec spec = { string_sv(&exe), args, countof(args), SV(""), NULL, 0 };
    String err = { 0 };
    app->area_picker = line_channel_start(&spec, &err);
    string_free(&exe);
    if (!app->area_picker) {
        app_set_status(app, "Board sync: %s", string_cstr(&err));
        string_free(&err);
        return;
    }
    app_set_status(app, "Board sync: drag a box around the board");
}

void app_sync_toggle(App *app) {
    if (app_sync_running(app)) {
        app_sync_stop(app);
        return;
    }
    ScreenRect area;
    if (parse_area(string_sv(&app->config.sync_area), &area)) start_sync(app, area);
    else app_sync_select(app);
}

static bool tree_contains(const GameNode *node, const GameNode *wanted) {
    for (const GameNode *c = node; c; c = c->next_sibling) {   // siblings iteratively, children recursively
        if (c == wanted || (c->first_child && tree_contains(c->first_child, wanted))) return true;
    }
    return false;
}

// Where the next reading continues: the node sync wrote last, while it is still
// in the open game (another file, a deleted node: from where the user is).
static GameNode *sync_anchor(App *app) {
    if (app->sync_node && app->sync_tree == app->tree && tree_contains(app->tree->root, app->sync_node)) return app->sync_node;
    return app->tree->current;
}

// A setup node (AB / AW / AE) after `at` that turns its position into the reading.
static GameNode *add_setup(App *app, GameNode *at, const BoardReading *r) {
    GameTree *tree = app->tree;
    GameNode *node = game_tree_append_child(tree, at);
    const Board *b = &at->board;
    i32 black = 0, white = 0;
    char coord[3];
    for (i32 i = 0; i < r->size * r->size; ++i) {
        black += r->cells[i] == STONE_BLACK, white += r->cells[i] == STONE_WHITE;
        if (b->cells[i] == r->cells[i]) continue;
        point_to_sgf((Point){ (i8)(i % r->size), (i8)(i / r->size) }, coord);
        StringView ident = r->cells[i] == STONE_BLACK ? SV("AB") : r->cells[i] == STONE_WHITE ? SV("AW") : SV("AE");
        game_node_add_prop_value(tree, node, ident, sv(coord));
    }
    // Who is to move is not on the screen: Black when the stones are even, else White.
    game_node_set_prop(tree, node, SV("PL"), black > white ? SV("W") : SV("B"));
    game_node_set_prop(tree, node, SV("C"), SV("Board sync"));
    game_tree_rebuild(tree, node);
    return node;
}

// The game continues after where sync left off, whatever the user is looking
// at; the view moves along only when it was on that node (following the game).
static void apply_reading(App *app, const BoardReading *r) {
    GameNode *at = sync_anchor(app), *next = at;
    bool following = app->tree->current == at;
    SyncPlan plan = board_sync_plan(at, r);
    switch (plan.change) {
        case SYNC_SAME: break;
        case SYNC_MOVES:
            for (i32 i = 0; i < plan.move_count && next; ++i) next = game_tree_play_at(app->tree, next, plan.moves[i], NULL);
            if (!next) next = at;
            break;
        case SYNC_BACK: next = (GameNode *)plan.node; break;
        case SYNC_SETUP: next = add_setup(app, at, r); break;
        case SYNC_NEW_GAME: {
            NewGameSpec spec = { .size = r->size, .komi = app->config.new_game_komi, .rules = SV("") };
            app_start_game(app, &spec);   // the view has to move: the old game is gone
            next = app->tree->root;
            if (board_sync_plan(next, r).change != SYNC_SAME) next = add_setup(app, next, r);
            following = true;
            break;
        }
    }
    if (plan.change != SYNC_SAME && plan.change != SYNC_BACK) app_mark_edited(app);
    static const char *const CHANGES[] = { "same", "moves", "back", "setup", "new game" };
    if (plan.change != SYNC_SAME) {
        log_info("board sync: %s -> move %d%s", CHANGES[plan.change], next->move_number, following ? " (view follows)" : "");
    }
    app->sync_node = next;
    app->sync_tree = app->tree;
    if (following && next != app->tree->current) app_goto(app, next);
}

void app_sync_update(App *app) {
    if (app->area_picker) {
        Line line;
        while (app->area_picker && line_channel_poll(app->area_picker, &line)) {
            ScreenRect area;
            if (line.kind == LINE_STDOUT && parse_area(string_sv(&line.text), &area)) {
                string_free(&app->config.sync_area);
                app->config.sync_area = string_from(string_sv(&line.text));
                line_channel_stop(app->area_picker, SV(""));
                app->area_picker = NULL;
                        start_sync(app, area);
            } else if (line.kind == LINE_EXITED) {
                line_channel_stop(app->area_picker, SV(""));
                app->area_picker = NULL;
                        app_set_status(app, "Board sync: no area picked");
            }
            line_free(&line);
        }
    }
    if (!app->board_sync) return;
    BoardReading reading;
    BoardSyncState state;
    String error = { 0 };
    bool fresh = board_sync_poll(app->board_sync, &reading, &state, &error);
    if (state != app->board_sync_state) {
        app->board_sync_state = state;
        if (state == BOARD_SYNC_RUNNING) app_set_status(app, "Board sync: following the board");
        if (state == BOARD_SYNC_FAILED) app_set_status(app, "Board sync stopped: %s", string_cstr(&error));
    }
    string_free(&error);
    if (state == BOARD_SYNC_FAILED) {
        board_sync_stop(app->board_sync);
        app->board_sync = NULL;
        return;
    }
    if (fresh) apply_reading(app, &reading);
}
