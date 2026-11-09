// app.h - application state and controller (no drawing).
//
// Owns the game tree, the engine, user config and the data-driven registries
// (commands, keymap, engine profiles, i18n). The UI layer reads App state and
// triggers changes only through commands and the app_* functions below.
#pragma once

#include "app/commands.h"
#include "app/config.h"
#include "app/i18n.h"
#include "app/keymap.h"
#include "core/sgf.h"
#include "engine/engine_profile.h"

typedef struct AppOptions {
    StringView sgf_path;
    StringView theme;        // overrides config
    StringView engine;       // overrides config (profile id), "none" disables
    StringView screenshot;   // render, save PNG, exit
    f64 screenshot_delay;    // seconds before capture
    bool portable;           // keep config next to the executable
    i32 start_move;          // advance N moves after loading (-1 = end)
    StringView run_commands[8];   // command ids executed once the UI is up (--run)
    i32 run_command_count;
} AppOptions;

enum { APP_LOG_LINES = 200 };

typedef struct App {
    AppOptions options;
    String exe_dir;
    String root_dir;     // install root (contains assets/ and katago/)
    String assets_dir;
    String config_dir;

    Config config;
    I18n i18n;
    Keymap keymap;
    CommandRegistry commands;
    EngineProfileVec engine_profiles;
    JsonDoc layout_doc;

    GameTree *tree;
    String sgf_path;
    bool dirty;

    Engine *engine;
    const EngineProfile *engine_profile;
    EngineStatus engine_status;
    String engine_error;
    bool analysis_enabled;
    const GameNode *synced_node;
    u64 synced_position_id;
    AnalysisSnapshot *analysis;   // latest snapshot for the current node
    bool analysis_valid;

    String log_lines[APP_LOG_LINES];
    i32 log_head, log_count;

    // Play vs AI: which colours the engine plays (indexed by Stone).
    bool ai_plays[3];
    bool ai_move_once;          // "engine, play one move now"
    bool ai_waiting;            // a request_move is outstanding

    u32 language_version;       // bumped when UI strings change (UI reloads fonts)
    String status_message;
    f64 status_until;
    bool quit_requested;
} App;

bool app_init(App *app, const AppOptions *options);
void app_shutdown(App *app);
// Pumps engine events and keeps the engine synced with the current node.
void app_update(App *app);

StringView app_tr(const App *app, const char *key, const char *fallback);
void app_set_status(App *app, const char *fmt, ...) PRINTF_LIKE(2, 3);
void app_log(App *app, StringView line);

void app_new_game(App *app, i32 size, f64 komi);
bool app_open_sgf(App *app, StringView path);
bool app_save_sgf(App *app, StringView path);
MoveError app_play(App *app, Point point);
void app_goto(App *app, GameNode *node);
void app_restart_engine(App *app);
void app_set_analysis(App *app, bool enabled);
AnalysisRequest app_analysis_request(const App *app);
void app_reload_language(App *app);
// Called after a setting changed through the generic config API (settings dialog);
// applies side effects such as restarting the engine or reloading strings.
void app_config_changed(App *app, StringView key);
