#include "app/board_sync.h"

#include "base/log.h"
#include "platform/os.h"
#include "platform/thread.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STOP_WAIT_SECONDS 0.3   // board_sync_stop waits this long for the thread, then leaves it to finish alone

struct BoardSync {
    BoardSyncSettings settings;
    String backend, command;   // owned copies behind settings' views
    Thread thread;
    Mutex lock;
    volatile bool quit;
    // Shared (under lock).
    BoardSyncState state;
    String error;
    BoardReading published;
    bool fresh;
    bool finished;    // the thread is about to return
    bool abandoned;   // board_sync_stop gave up waiting: the thread frees this itself
};

static void board_sync_free(BoardSync *s) {
    mutex_destroy(&s->lock);
    string_free(&s->error);
    string_free(&s->backend);
    string_free(&s->command);
    free(s);
}

static void set_state(BoardSync *s, BoardSyncState state, StringView error) {
    mutex_lock(&s->lock);
    s->state = state;
    string_free(&s->error);
    s->error = string_from(error);
    mutex_unlock(&s->lock);
}

static i32 sync_loop(BoardSync *s);

// One capture can take long (the Wayland portal waits for the desktop's answer):
// a stopped sync whose thread is still capturing cleans up after itself.
static i32 sync_main(void *arg) {
    BoardSync *s = arg;
    sync_loop(s);
    mutex_lock(&s->lock);
    s->finished = true;
    bool abandoned = s->abandoned;
    mutex_unlock(&s->lock);
    if (abandoned) board_sync_free(s);
    return 0;
}

static i32 sync_loop(BoardSync *s) {
    String err = { 0 };
    ScreenCapture *capture = screen_capture_open(s->settings.backend, s->settings.command, &err);
    if (!capture) {
        set_state(s, BOARD_SYNC_FAILED, string_sv(&err));
        string_free(&err);
        return 0;
    }
    screen_capture_set_cancel(capture, &s->quit);   // a capture waiting for the desktop stops with the sync
    BoardGrid grid = { 0 };
    BoardReading last = { 0 }, candidate = { 0 };
    bool have_candidate = false, have_last = false;
    i32 failures = 0;
    while (!s->quit) {
        f64 started = os_time_seconds();
        RgbaImage image;
        if (!screen_capture_grab(capture, &s->settings.area, &image, &err)) {
            if (++failures >= 5) {   // a few in a row: give up (the portal may have been refused)
                set_state(s, BOARD_SYNC_FAILED, string_sv(&err));
                break;
            }
            string_clear(&err);
        } else {
            failures = 0;
            if (grid.size == 0 && !board_grid_detect(&image, s->settings.size, &grid)) {
                // Keep what was captured: it shows whether the right part of the screen came back.
                String png = { 0 }, dir = os_config_dir(), file = path_join(string_sv(&dir), SV("sync-capture.png"));
                png_encode(&image, &png);
                bool saved = os_make_dirs(string_sv(&dir)) && os_write_file(string_sv(&file), string_sv(&png));
                const ScreenRect *a = &s->settings.area;
                log_warn("board sync: no board in the %dx%d capture of the area %d,%d %dx%d%s%s", image.width, image.height, a->x, a->y, a->width,
                         a->height, saved ? "; saved to " : "", saved ? string_cstr(&file) : "");
                String msg = saved ? string_format("no Go board found in the selected area (the captured picture: %s)", string_cstr(&file))
                                   : string_from(SV("no Go board found in the selected area"));
                set_state(s, BOARD_SYNC_FAILED, string_sv(&msg));
                string_free(&msg), string_free(&png), string_free(&dir), string_free(&file);
                rgba_image_free(&image);
                break;
            }
            if (s->state == BOARD_SYNC_STARTING) {
                log_info("board sync: %dx%d grid, step %.1f px", grid.size, grid.size, (f64)grid.step_x);
                set_state(s, BOARD_SYNC_RUNNING, SV(""));
            }
            // The board must still be there (not covered, minimized or half drawn).
            bool visible = board_grid_strength(&image, &grid) >= fmaxf(0.08f, 0.2f * grid.strength);
            if (!visible) {
                rgba_image_free(&image);
                have_candidate = false;
                i32 wait = s->settings.interval_ms;
                for (; wait > 0 && !s->quit; wait -= 20) os_sleep_ms(MIN(wait, 20));
                continue;
            }
            BoardReading reading;
            board_read_stones(&image, &grid, &reading);
            rgba_image_free(&image);
            // Stable: the same twice in a row, and new.
            bool stable = have_candidate && memcmp(&candidate, &reading, sizeof(reading)) == 0;
            candidate = reading, have_candidate = true;
            if (stable && (!have_last || memcmp(&last, &reading, sizeof(reading)) != 0)) {
                last = reading, have_last = true;
                mutex_lock(&s->lock);
                s->published = reading;
                s->fresh = true;
                mutex_unlock(&s->lock);
            }
        }
        i32 wait = s->settings.interval_ms - (i32)((os_time_seconds() - started) * 1000.0);
        for (; wait > 0 && !s->quit; wait -= 20) os_sleep_ms(MIN(wait, 20));
    }
    string_free(&err);
    screen_capture_close(capture);
    return 0;
}

BoardSync *board_sync_start(const BoardSyncSettings *settings) {
    BoardSync *s = calloc(1, sizeof(BoardSync));
    if (!s) abort();
    s->backend = string_from(settings->backend);
    s->command = string_from(settings->command);
    s->settings = *settings;
    s->settings.backend = string_sv(&s->backend);
    s->settings.command = string_sv(&s->command);
    s->settings.interval_ms = MAX(settings->interval_ms, 50);
    mutex_init(&s->lock);
    if (!thread_start(&s->thread, sync_main, s)) {
        s->state = BOARD_SYNC_FAILED;
        s->error = string_from(SV("cannot start the sync thread"));
    }
    return s;
}

bool board_sync_poll(BoardSync *s, BoardReading *out, BoardSyncState *state, String *error) {
    mutex_lock(&s->lock);
    bool fresh = s->fresh;
    if (fresh) *out = s->published;
    s->fresh = false;
    *state = s->state;
    if (error) {
        string_free(error);
        *error = string_from(string_sv(&s->error));
    }
    mutex_unlock(&s->lock);
    return fresh;
}

// Never blocks the caller (the UI) for long: a thread still inside a capture is
// left to finish it and free the sync itself.
void board_sync_stop(BoardSync *s) {
    if (!s) return;
    s->quit = true;
    if (!s->thread.handle) {
        board_sync_free(s);
        return;
    }
    // A deadline, not a count of sleeps: sleeps can oversleep a lot (macOS under load).
    for (f64 deadline = os_time_seconds() + STOP_WAIT_SECONDS; os_time_seconds() < deadline;) {
        mutex_lock(&s->lock);
        bool finished = s->finished;
        mutex_unlock(&s->lock);
        if (finished) {
            thread_join(&s->thread);
            board_sync_free(s);
            return;
        }
        os_sleep_ms(10);
    }
    Thread thread = s->thread;   // copied first: once abandoned, the thread may free `s`
    mutex_lock(&s->lock);
    bool finished = s->finished;
    if (!finished) s->abandoned = true;
    mutex_unlock(&s->lock);
    if (finished) {
        thread_join(&thread);
        board_sync_free(s);
    } else {
        log_info("board sync: the capture is still running; stopping without waiting for it");
        thread_detach(&thread);
    }
}

// ---- Plan -------------------------------------------------------------------------------------------

static bool same_cells(const Board *b, const BoardReading *r) {
    for (i32 i = 0; i < r->size * r->size; ++i) {
        if (b->cells[i] != r->cells[i]) return false;
    }
    return true;
}

// Plays `moves` on a copy of `from`; true if legal and the result is the reading.
static bool explains(const Board *from, const Move *moves, i32 count, const BoardReading *r) {
    Board b = *from;
    for (i32 i = 0; i < count; ++i) {
        if (board_play(&b, moves[i], (BoardRules){ 0 }) != MOVE_OK) return false;
    }
    return same_cells(&b, r);
}

SyncPlan board_sync_plan(const GameNode *cur, const BoardReading *r) {
    SyncPlan plan = { SYNC_SETUP };
    const Board *b = &cur->board;
    if (b->width != r->size || b->height != r->size) return (SyncPlan){ SYNC_NEW_GAME };
    if (same_cells(b, r)) return (SyncPlan){ SYNC_SAME };
    // Stones that appeared (moves, possibly with captures elsewhere).
    Move added[3];
    i32 n_added = 0;
    for (i32 i = 0; i < r->size * r->size; ++i) {
        if (r->cells[i] == STONE_EMPTY || b->cells[i] == r->cells[i]) continue;
        if (n_added == 3) {
            n_added = 4;
            break;
        }
        added[n_added++] = (Move){ (Stone)r->cells[i], { (i8)(i % r->size), (i8)(i / r->size) } };
    }
    if (n_added == 1 && explains(b, added, 1, r)) {
        plan.change = SYNC_MOVES, plan.moves[0] = added[0], plan.move_count = 1;
        return plan;
    }
    if (n_added == 2 && added[0].color != added[1].color) {   // two moves between captures
        Move first = added[0].color == cur->to_play ? added[0] : added[1], second = added[0].color == cur->to_play ? added[1] : added[0];
        Move order[2][2] = { { first, second }, { second, first } };   // the side to move first, then the other way round
        for (i32 k = 0; k < 2; ++k) {
            if (explains(b, order[k], 2, r)) {
                plan.change = SYNC_MOVES, plan.moves[0] = order[k][0], plan.moves[1] = order[k][1], plan.move_count = 2;
                return plan;
            }
        }
    }
    // Undo / navigation in the other program: an earlier position of this line.
    for (const GameNode *n = cur->parent; n; n = n->parent) {
        if (same_cells(&n->board, r)) return (SyncPlan){ SYNC_BACK, .node = n };
    }
    return plan;
}
