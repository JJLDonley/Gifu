#include "app/config.h"

#include "base/json.h"
#include "base/log.h"
#include "base/os.h"

#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// ---- Field table --------------------------------------------------------------------------

#define STR(key, field, def) { key, CONFIG_STRING, offsetof(Config, field), 0, 0, def, 0 }
#define BOOL(key, field, def) { key, CONFIG_BOOL, offsetof(Config, field), 0, 1, NULL, def }
#define INT(key, field, def, lo, hi) { key, CONFIG_INT, offsetof(Config, field), lo, hi, NULL, def }
#define FLOAT(key, field, def, lo, hi) { key, CONFIG_FLOAT, offsetof(Config, field), lo, hi, NULL, def }

static const ConfigField FIELDS[] = {
    STR("theme", theme, "cream"),
    STR("language", language, "en"),
    FLOAT("ui_scale", ui_scale, 1.0, 0.5, 3.0),
    STR("engine", engine, ""),
    STR("rules", rules, "chinese"),
    STR("last_directory", last_directory, ""),
    BOOL("analysis.on_start", analyze_on_start, 1),
    INT("analysis.interval_cs", analysis_interval_cs, 10, 5, 100),
    INT("analysis.max_candidates", max_candidates, 12, 1, 48),
    BOOL("analysis.show_ownership", show_ownership, 0),
    BOOL("analysis.show_candidates", show_candidates, 1),
    BOOL("board.show_coordinates", show_coordinates, 1),
    BOOL("board.show_move_numbers", show_move_numbers, 0),
    FLOAT("ai.seconds_per_move", ai_seconds_per_move, 2.0, 0.1, 120.0),
    INT("new_game.size", new_game_size, 19, 5, 25),
    FLOAT("new_game.komi", new_game_komi, 7.5, -150.0, 150.0),
    INT("new_game.handicap", new_game_handicap, 0, 0, 9),
    STR("new_game.black", new_game_black, ""),
    STR("new_game.white", new_game_white, ""),
    INT("window.width", window_width, 1400, 640, 16384),
    INT("window.height", window_height, 900, 480, 16384),
};

#undef STR
#undef BOOL
#undef INT
#undef FLOAT

isize config_field_count(void) { return countof(FIELDS); }
const ConfigField *config_field_at(isize index) { return index >= 0 && index < countof(FIELDS) ? &FIELDS[index] : NULL; }

const ConfigField *config_field(StringView key) {
    for (isize i = 0; i < countof(FIELDS); ++i) {
        if (sv_eq(sv(FIELDS[i].key), key)) return &FIELDS[i];
    }
    return NULL;
}

static void *field_ptr(const Config *config, const ConfigField *f) { return (u8 *)config + f->offset; }

bool config_get_bool(const Config *config, const ConfigField *f) { return *(const bool *)field_ptr(config, f); }

f64 config_get_number(const Config *config, const ConfigField *f) {
    switch (f->type) {
        case CONFIG_INT: return *(const i32 *)field_ptr(config, f);
        case CONFIG_FLOAT: return *(const f32 *)field_ptr(config, f);
        case CONFIG_BOOL: return *(const bool *)field_ptr(config, f) ? 1 : 0;
        case CONFIG_STRING: return 0;
    }
    return 0;
}

StringView config_get_string(const Config *config, const ConfigField *f) {
    return f->type == CONFIG_STRING ? string_sv((const String *)field_ptr(config, f)) : SV("");
}

void config_set_bool(Config *config, const ConfigField *f, bool value) {
    if (f->type == CONFIG_BOOL) *(bool *)field_ptr(config, f) = value;
}

void config_set_number(Config *config, const ConfigField *f, f64 value) {
    value = CLAMP(value, f->min, f->max);
    if (f->type == CONFIG_INT) *(i32 *)field_ptr(config, f) = (i32)llround(value);
    else if (f->type == CONFIG_FLOAT) *(f32 *)field_ptr(config, f) = (f32)value;
}

void config_set_string(Config *config, const ConfigField *f, StringView value) {
    if (f->type != CONFIG_STRING) return;
    String *s = field_ptr(config, f);
    String copy = string_from(value);   // value may alias *s
    string_free(s);
    *s = copy;
}

// ---- Lifecycle --------------------------------------------------------------------------------

void config_init_defaults(Config *config, StringView path) {
    *config = (Config){ 0 };
    config->path = string_from(path);
    for (isize i = 0; i < countof(FIELDS); ++i) {
        const ConfigField *f = &FIELDS[i];
        switch (f->type) {
            case CONFIG_STRING: *(String *)field_ptr(config, f) = string_from(sv(f->default_string)); break;
            case CONFIG_BOOL: config_set_bool(config, f, f->default_number != 0); break;
            case CONFIG_INT:
            case CONFIG_FLOAT: config_set_number(config, f, f->default_number); break;
        }
    }
}

// One-time migration from the old config.json (same dotted keys as nested objects).
static void migrate_json(Config *config) {
    String json_path_str = string_format(SV_FMT "/config.json", SV_ARG(path_dirname(string_sv(&config->path))));
    JsonDoc doc = { 0 };
    if (os_file_exists(string_sv(&json_path_str)) && json_load_file(string_sv(&json_path_str), &doc, NULL)) {
        JsonValue root = json_root(&doc);
        for (isize i = 0; i < countof(FIELDS); ++i) {
            const ConfigField *f = &FIELDS[i];
            JsonValue v = json_path(root, f->key);
            if (!json_exists(v)) continue;
            if (f->type == CONFIG_STRING && json_is_string(v)) config_set_string(config, f, json_string(v, SV("")));
            else if (f->type == CONFIG_BOOL) config_set_bool(config, f, json_bool(v, config_get_bool(config, f)));
            else if (json_is_number(v)) config_set_number(config, f, json_number(v, 0));
        }
        log_info("migrated settings from %s", string_cstr(&json_path_str));
    }
    json_free(&doc);
    string_free(&json_path_str);
}

bool config_load(Config *config, String *error) {
    UNUSED(error);
    KvFile kv = { 0 };
    if (!kv_load(&kv, string_sv(&config->path))) {   // no config.kv yet
        migrate_json(config);
        return true;
    }
    vec_foreach(KvEntry, e, &kv.entries) {
        const ConfigField *f = config_field(e->key);
        if (!f) {
            log_warn("%s:%d: unknown setting '" SV_FMT "'", string_cstr(&config->path), e->line, SV_ARG(e->key));
            continue;
        }
        f64 number = 0;
        bool flag = false;
        switch (f->type) {
            case CONFIG_STRING: config_set_string(config, f, e->value); break;
            case CONFIG_BOOL:
                if (kv_parse_bool(e->value, &flag)) config_set_bool(config, f, flag);
                break;
            case CONFIG_INT:
            case CONFIG_FLOAT:
                if (sv_parse_f64(e->value, &number)) config_set_number(config, f, number);
                break;
        }
    }
    kv_free(&kv);
    return true;
}

bool config_save(const Config *config) {
    String out = string_from(SV("# CataGui settings (written by the app; edit while it is closed).\n"
                                "# Keymap, engines, layout and themes overrides live next to this file:\n"
                                "# keymap.kv, engines.kv, layout.lisp, themes/*.theme\n"));
    StringView group = SV_NULL;
    for (isize i = 0; i < countof(FIELDS); ++i) {
        const ConfigField *f = &FIELDS[i];
        StringView key = sv(f->key);
        isize dot = sv_find_char(key, '.');
        StringView g = dot > 0 ? sv_take(key, dot) : SV("");
        if (!group.data || !sv_eq(g, group)) string_push_char(&out, '\n');
        group = g;
        switch (f->type) {
            case CONFIG_STRING: {
                StringView v = config_get_string(config, f);
                bool quote = v.len && (v.data[0] == ' ' || v.data[v.len - 1] == ' ' || v.data[0] == '"');
                string_pushf(&out, "%s = %s" SV_FMT "%s\n", f->key, quote ? "\"" : "", SV_ARG(v), quote ? "\"" : "");
                break;
            }
            case CONFIG_BOOL: string_pushf(&out, "%s = %s\n", f->key, config_get_bool(config, f) ? "true" : "false"); break;
            case CONFIG_INT: string_pushf(&out, "%s = %d\n", f->key, (i32)config_get_number(config, f)); break;
            case CONFIG_FLOAT: string_pushf(&out, "%s = %g\n", f->key, round(config_get_number(config, f) * 1000.0) / 1000.0); break;
        }
    }
    os_make_dirs(path_dirname(string_sv(&config->path)));
    bool ok = os_write_file(string_sv(&config->path), string_sv(&out));
    string_free(&out);
    if (!ok) log_warn("could not write %s", string_cstr(&config->path));
    return ok;
}

void config_free(Config *config) {
    for (isize i = 0; i < countof(FIELDS); ++i) {
        if (FIELDS[i].type == CONFIG_STRING) string_free((String *)field_ptr(config, &FIELDS[i]));
    }
    string_free(&config->path);
}
