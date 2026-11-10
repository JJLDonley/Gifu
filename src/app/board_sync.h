// board_sync.h - follows a board shown by another program on the screen.
//
// A worker thread captures the chosen screen area every interval, finds the
// grid once (board_grid_detect), then reads the stones. A reading is published
// only after it was seen twice in a row (stone animations, half-drawn frames),
// and only when it differs from the last one published. The app turns
// readings into moves / positions (board_sync_apply).
#pragma once

#include "app/screen_capture.h"
#include "core/board_reader.h"
#include "core/game_tree.h"

typedef struct BoardSyncSettings {
    StringView backend, command;   // screen_capture_open
    ScreenRect area;               // desktop coordinates
    i32 size;                      // 0 = detect 19 / 13 / 9
    i32 interval_ms;
} BoardSyncSettings;

typedef enum BoardSyncState {
    BOARD_SYNC_STARTING,   // capturing / looking for the grid
    BOARD_SYNC_RUNNING,
    BOARD_SYNC_FAILED,     // see error; the worker has stopped
} BoardSyncState;

typedef struct BoardSync BoardSync;

BoardSync *board_sync_start(const BoardSyncSettings *settings);
// New stable reading since the last call? `state` / `error` (owned copy) are always filled.
bool board_sync_poll(BoardSync *sync, BoardReading *out, BoardSyncState *state, String *error);
void board_sync_stop(BoardSync *sync);   // joins the worker

// ---- Applying a reading to the game (pure, unit-tested) ---------------------------------------------

typedef enum SyncChange {
    SYNC_SAME,        // nothing to do
    SYNC_MOVES,       // `moves[0..move_count)` were played from the current position
    SYNC_BACK,        // the reading is an earlier position of the current line: `node`
    SYNC_SETUP,       // anything else: a setup node with these stones
    SYNC_NEW_GAME,    // other board size
} SyncChange;

typedef struct SyncPlan {
    SyncChange change;
    Move moves[2];
    i32 move_count;
    const GameNode *node;
} SyncPlan;

// Explains `reading` from node `from` (where sync left off): up to two moves
// (alternating colours, captures included), an earlier position on its line, or a setup.
SyncPlan board_sync_plan(const GameNode *from, const BoardReading *reading);
