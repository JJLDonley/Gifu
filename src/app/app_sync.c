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
        app->window_request = APP_WINDOW_RESTORE;
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
    app->window_request = APP_WINDOW_MINIMIZE;   // out of the way of the other program
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

// A setup node (AB / AW / AE) that turns the current position into the reading.
static void apply_setup(App *app, const BoardReading *r) {
    GameTree *tree = app->tree;
    GameNode *node = game_tree_append_child(tree, tree->current);
    const Board *b = &tree->current->board;
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
    game_tree_goto(tree, node);
}

static void apply_reading(App *app, const BoardReading *r) {
    SyncPlan plan = board_sync_plan(app->tree, r);
    switch (plan.change) {
        case SYNC_SAME: return;
        case SYNC_MOVES:
            for (i32 i = 0; i < plan.move_count; ++i) game_tree_play(app->tree, plan.moves[i]);
            break;
        case SYNC_BACK: app_goto(app, (GameNode *)plan.node); return;
        case SYNC_NEW_GAME: {
            NewGameSpec spec = { .size = r->size, .komi = app->config.new_game_komi, .rules = SV("") };
            app_start_game(app, &spec);
            if (board_sync_plan(app->tree, r).change != SYNC_SAME) apply_setup(app, r);
            break;
        }
        case SYNC_SETUP: apply_setup(app, r); break;
    }
    app_mark_edited(app);
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
                app->window_request = APP_WINDOW_RESTORE;
                start_sync(app, area);
            } else if (line.kind == LINE_EXITED) {
                line_channel_stop(app->area_picker, SV(""));
                app->area_picker = NULL;
                app->window_request = APP_WINDOW_RESTORE;
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
