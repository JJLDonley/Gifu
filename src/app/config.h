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
    String engine;         // engine profile id
    String engine2;
    String install_backend;
    String human_profile;     // humanSLProfile for engines with a human network (rank_5k, preaz_1d, proyear_1990...)   // KataGo build the installer downloads (opencl, eigenavx2, ...)        // second engine (comparison / engine-vs-engine); "" = same as main
    String rules;          // KataGo rules name
    bool analyze_on_start;
    i32 analysis_interval_cs;
    i32 max_candidates;
    bool show_ownership;
    bool show_coordinates;
    bool show_candidates;
    bool show_move_numbers;
    f32 ai_seconds_per_move;
    i32 analysis_game_visits;
    i32 new_game_size;
    f32 new_game_komi;
    i32 new_game_handicap;
    String new_game_black;
    String new_game_white;
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
