// config.h - user settings (<config_dir>/config.kv, key=value).
//
// Every setting is described once in a field table (config.c): its dotted
// JSON key, type, default and range. Loading, saving, the settings dialog
// (assets/data/forms.lisp) and change notifications all go through that
// table, so adding a setting is one line plus data. User overrides for other
// data live beside it: keymap.kv, engines.kv, layout.lisp, themes/.
#pragma once

#include "base/kv.h"

typedef struct Config {
    String path;
    String theme;          // theme id, e.g. "light", "dark", "cream", "coffee-paper"
    String language;       // "en", "zh_CN", "zh_TW", "ja", "ko"
    f32 ui_scale;
    bool ui_scale_with_window;   // shrink / grow the interface with the window (around 1400x900)
    String engine;         // engine profile id
    String engine2;
    String install_backend;
    String human_profile;     // humanSLProfile for engines with a human network (rank_5k, preaz_1d, proyear_1990...)   // KataGo build the installer downloads (opencl, eigenavx2, ...)        // second engine (comparison / engine-vs-engine); "" = same as main
    String human_white_rank;  // "" = same as Black; else White imitates this rank ("1d") -> rank_{B}_{W}
    String rules;          // KataGo rules name
    bool analyze_on_start;
    i32 analysis_interval_cs;
    i32 max_candidates;
    bool show_ownership;
    bool show_human_policy;   // human SL network moves on the board (needs a human_model engine)
    bool show_coordinates;
    bool show_candidates;
    bool show_move_numbers;
    f32 ai_seconds_per_move;
    bool ai_human_style;      // engine moves come from the human network at ai.human_profile
    bool ai_show_eval;          // win rate, score, graph, candidates... while playing the engine (hidden by default)
    i32 engine_max_processes;
    i32 report_compare_rank;
    bool sgf_autosave;         // save the game (with its analysis) automatically
    i32 sgf_autosave_seconds;  // how often analysis changes are saved (edits: a few seconds after)
    bool sgf_save_lz;         // also write analysis as LZ[] (Lizzie / LizzieYzy)
    bool sgf_save_kt;         // also write analysis as KT[] (KaTrain)
    String tsumego_attacker;  // "auto", "black" or "white" (Tsumego frame)
    i32 tsumego_margin;       // empty lines between the problem and the frame's wall
    String sync_capture;         // screen capture backend for board sync: auto, gdi, x11, portal, grim, command
    String sync_capture_command; // "command" backend: writes a PNG to stdout, or to %f
    i32 sync_interval_ms;        // board sync: time between captures
    String sync_area;            // board sync: last picked area, "x,y,w,h" in desktop coordinates
    String tsumego_ko_threats;   // "none", "attacker", "defender" or "both": who gets a ko threat in the frame
    // KataGo settings sent as -override-config (0 / -1 = keep the .cfg value).
    i32 katago_search_threads, katago_max_visits, katago_gpu, katago_nn_batch, katago_nn_cache_power;
    i32 katago_analysis_threads, katago_analysis_search_threads;
    String katago_overrides;  // Deep report: rank index (0 = 20k .. 11 = 9d) the players are compared with // KataGo processes the GPU can hold at once (1 = one engine does everything)
    i32 analysis_game_visits;
    i32 new_game_size;
    f32 new_game_komi;
    i32 new_game_handicap;
    String new_game_black;
    String new_game_white;
    String new_game_opponent;   // "none", "black" or "white": the colour the engine plays in a new game
    // Game info dialog (transient: copied from / to the SGF root).
    String game_info_black, game_info_white, game_info_black_rank, game_info_white_rank;
    String game_info_result, game_info_event, game_info_date;
    f32 game_info_komi;
    i32 window_width, window_height;
    String last_directory;
} Config;

typedef enum ConfigType { CONFIG_BOOL, CONFIG_INT, CONFIG_FLOAT, CONFIG_STRING } ConfigType;

typedef struct ConfigField {
    const char *key;       // dotted JSON path, e.g. "analysis.interval_cs"
    ConfigType type;
    usize offset;          // into Config
    f64 min, max;          // numeric range
    const char *default_string;
    f64 default_number;
    bool transient;        // scratch value for a dialog: never loaded from or saved to config.kv
} ConfigField;

void config_init_defaults(Config *config, StringView path);
// Missing file is not an error (defaults are kept).
bool config_load(Config *config, String *error);
NODISCARD bool config_save(const Config *config);
void config_free(Config *config);

// ---- Field access (used by the data-driven settings dialog) -----------------------------
const ConfigField *config_field(StringView key);
isize config_field_count(void);
const ConfigField *config_field_at(isize index);
bool config_get_bool(const Config *config, const ConfigField *field);
f64 config_get_number(const Config *config, const ConfigField *field);
StringView config_get_string(const Config *config, const ConfigField *field);
void config_set_bool(Config *config, const ConfigField *field, bool value);
void config_set_number(Config *config, const ConfigField *field, f64 value);   // clamped
void config_set_string(Config *config, const ConfigField *field, StringView value);
