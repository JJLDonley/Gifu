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
#include "engine/game_job.h"

typedef struct AppOptions {
    StringView sgf_path;
    StringView theme;        // overrides config
    StringView engine;       // overrides config (profile id), "none" disables
    StringView screenshot;   // render, save PNG, exit
    f64 screenshot_delay;    // seconds before capture
    bool portable;           // keep config next to the executable
    i32 start_move;          // advance N moves after loading (-1 = end)
    i32 window_width, window_height;   // --size WxH (0 = from config)
    StringView run_commands[8];   // command ids executed once the UI is up (--run)
    i32 run_command_count;
    StringView settings[16];      // "key=value" config overrides (--set)
    i32 setting_count;
    StringView analyze_files[256];   // --analyze: headless batch analysis
    i32 analyze_count;
    StringView output_dir;           // --out
    bool headless;                   // no window, no interactive engine
    StringView region;               // --region A1:G7 (tsumego focus)
    StringView install;              // --install engine,network,human (headless)
    StringView install_backend;      // --backend opencl|eigenavx2|eigen|cuda12.8-cudnn9.8.0
    StringView install_dir;          // --install-dir (default <root>/katago)
} AppOptions;

enum { APP_LOG_LINES = 200 };

// One running engine and what it currently holds.
typedef struct EngineSlot {
    Engine *engine;
    const EngineProfile *profile;
    EngineStatus status;
    String error;
    GameNode *synced_node;       // node the engine currently holds
    u64 synced_position_id;
    AnalysisSnapshot *analysis;  // latest snapshot for synced_node (second slot; main uses App.analysis)
    bool analysis_valid;
    bool waiting_move;           // a request_move is outstanding
} EngineSlot;

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

    GameTree *tree;
    String sgf_path;
    bool dirty;

    // Engines: `main` drives analysis, the board and the graph; `second` is an
    // optional extra engine for side-by-side comparison and engine-vs-engine games.
    EngineSlot main;
    EngineSlot second;
    bool analysis_enabled;
    AnalysisSnapshot *analysis;   // latest snapshot for the current node
    bool analysis_valid;

    String log_lines[APP_LOG_LINES];
    i32 log_head, log_count;

    // Whole-game analysis: the engine walks these nodes, analysing each one to
    // config.analysis_game_visits, while the user keeps navigating freely.
    // Two strategies: the JSON analyzer (whole game in one query, fast) when
    // the engine profile has an analysis_config, else walking positions over GTP.
    bool batch_running;
    bool batch_restore_analysis;   // analysis_enabled before the batch started
    GameNode **batch_nodes;        // GTP walk
    isize batch_count, batch_index;
    BatchAnalyzer *analyzer;       // JSON analyzer (started on first use)
    GameJob flash_job;
    bool flash_running;
    u32 flash_serial;

    BoardRegion region;         // tsumego: analysis restricted to this rectangle

    // Play vs AI: which colours the engine plays (indexed by Stone).
    bool ai_plays[3];
    bool ai_move_once;          // "engine, play one move now"
    bool match_mode;            // engine vs engine: main plays Black, second plays White

    u32 language_version;       // bumped when UI strings change (UI reloads fonts)

    // Game result (engine final_score), shown as a banner for its position.
    String result;
    u64 result_position;
    bool score_requested;       // final_score sent, waiting for the answer
    bool score_pending;         // user asked; send once the engine is ready and synced
    const GameNode *auto_scored_node;   // two passes already handled here
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

void app_start_game(App *app, const NewGameSpec *spec);
bool app_open_sgf(App *app, StringView path);
bool app_save_sgf(App *app, StringView path);
MoveError app_play(App *app, Point point);
void app_goto(App *app, GameNode *node);
void app_restart_engine(App *app);
void app_second_engine(App *app, bool on);
// Restricts (or with region.active=false, frees) analysis to a board rectangle.
void app_set_region(App *app, BoardRegion region);
// Where KataGo lives: <root>/katago (created by the installer).
String app_katago_dir(const App *app);
// Re-reads engine profiles (e.g. after an install) and restarts the engine.
void app_reload_engines(App *app);   // start/stop the comparison engine
void app_set_analysis(App *app, bool enabled);
AnalysisRequest app_analysis_request(const App *app);
void app_reload_language(App *app);
// Called after a setting changed through the generic config API (settings dialog);
// applies side effects such as restarting the engine or reloading strings.
void app_config_changed(App *app, StringView key);
void app_batch_start(App *app);   // analyse the active line from the start
void app_batch_stop(App *app);
bool app_game_analysis_running(const App *app);
// Progress of the running whole-game analysis (positions done / total).
void app_game_analysis_progress(const App *app, isize *done, isize *total);
