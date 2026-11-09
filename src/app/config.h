// config.h - user settings (<config_dir>/config.json).
//
// Every setting is described once in a field table (config.c): its dotted
// JSON key, type, default and range. Loading, saving, the settings dialog
// (assets/data/settings.json) and change notifications all go through that
// table, so adding a setting is one line plus data.
//
// The file is also the user's override layer for data: it may contain
// "engines" (profiles), "keymap" ({"bindings": [...]}) and "layout" sections
// that are merged over the shipped assets/data/*.json.
#pragma once

#include "base/json.h"

typedef struct Config {
    String path;
    String theme;          // theme id, e.g. "light", "dark", "cream", "coffee-paper"
    String language;       // "en", "zh_CN", "zh_TW", "ja", "ko"
    f32 ui_scale;
    String engine;         // engine profile id
    String rules;          // KataGo rules name
    bool analyze_on_start;
    i32 analysis_interval_cs;
    i32 max_candidates;
    bool show_ownership;
    bool show_coordinates;
    bool show_candidates;
    bool show_move_numbers;
    f32 ai_seconds_per_move;
    i32 window_width, window_height;
    String last_directory;

    JsonDoc doc;           // the raw file, kept for override sections
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
JsonValue config_section(const Config *config, const char *key);   // e.g. "engines"
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
