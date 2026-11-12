#include "app/config.h"

#include "base/json.h"
#include "base/log.h"
#include "platform/os.h"

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
#define TSTR(key, field) { key, CONFIG_STRING, offsetof(Config, field), 0, 0, "", 0, true }
#define TFLOAT(key, field, def, lo, hi) { key, CONFIG_FLOAT, offsetof(Config, field), lo, hi, NULL, def, true }

static const ConfigField FIELDS[] = {
    STR("theme", theme, "cream"),
    STR("language", language, "en"),
    FLOAT("ui_scale", ui_scale, 1.0, 0.5, 3.0),
    BOOL("ui_scale_with_window", ui_scale_with_window, 1),
    STR("engine", engine, ""),
    STR("engine2", engine2, ""),
    STR("ai.engine", ai_engine, ""),
    STR("ai.style", ai_style, "strongest"),
    STR("ai.layout", ai_layout, "play"),   // layout preset while the AI plays ("" = keep the current one)
    STR("analysis.layout", analysis_layout, "analysis"),   // layout preset when no AI plays, from startup ("" = layout.lisp)   // strongest | human | territorial | influence | fighting | calm (app/ai_style.h)
    STR("profile.names", profile_names, ""),     // the user's player names, comma-separated
    STR("profile.folder", profile_folder, ""),   // where the user's games are   // the engine that plays against you ("" = the main engine)
    STR("install.backend", install_backend, "opencl"),
    STR("ai.human_profile", human_profile, "rank_5k"),
    STR("ai.human_white_rank", human_white_rank, ""),
    STR("rules", rules, "chinese"),
    STR("last_directory", last_directory, ""),
    BOOL("analysis.on_start", analyze_on_start, 1),
    INT("analysis.interval_cs", analysis_interval_cs, 10, 5, 100),
    INT("analysis.max_candidates", max_candidates, 12, 1, 48),
    INT("analysis.game_visits", analysis_game_visits, 300, 20, 100000),
    BOOL("analysis.show_ownership", show_ownership, 0),
    BOOL("analysis.show_human_policy", show_human_policy, 0),
    BOOL("analysis.show_candidates", show_candidates, 1),
    BOOL("board.show_coordinates", show_coordinates, 1),
    INT("board.move_numbers", move_numbers, 0, -1, 999),   // -1 all, 0 none, N the last N moves
    BOOL("board.show_branches", show_branches, 0),
    BOOL("board.drag_stones", drag_stones, 1),
    FLOAT("ai.seconds_per_move", ai_seconds_per_move, 10.0, 0.0, 120.0),   // 0 = no clock: the engine's own limits
    INT("ai.max_visits", ai_max_visits, 1000, 0, 10000000),                 // KataGo's search per move when it plays (0 = its config)
    FLOAT("human_winrate.level", human_winrate_level, 1.0, -11.0, 9.0),   // Fox rank: -11 = 12k, 0 = 1k, 1 = 1d, 9 = 9d
    BOOL("ai.human_style", ai_human_style, 0),
    BOOL("ai.show_eval", ai_show_eval, 0),
    INT("katago.search_threads", katago_search_threads, 0, 0, 256),
    INT("katago.max_visits", katago_max_visits, 0, 0, 10000000),
    INT("katago.gpu", katago_gpu, -1, -1, 15),
    INT("katago.nn_batch", katago_nn_batch, 0, 0, 1024),
    INT("katago.nn_cache_power", katago_nn_cache_power, 0, 0, 30),
    INT("katago.analysis_threads", katago_analysis_threads, 12, 1, 256),
    INT("katago.analysis_search_threads", katago_analysis_search_threads, 2, 1, 256),
    STR("katago.overrides", katago_overrides, ""),
    STR("katago.network", katago_network, ""),               // file in katago/networks ("" = the newest)
    STR("katago.human_network", katago_human_network, ""),   // file in katago/networks/human
    BOOL("sgf.autosave", sgf_autosave, 0),
    INT("sgf.autosave_seconds", sgf_autosave_seconds, 60, 10, 3600),
    BOOL("sgf.save_lz", sgf_save_lz, 0),
    BOOL("sgf.save_kt", sgf_save_kt, 0),
    STR("tsumego.attacker", tsumego_attacker, "auto"),
    STR("tsumego.ko_threats", tsumego_ko_threats, "none"),
    BOOL("updates.check", updates_check, 1),
    STR("updates.releases", updates_releases, "auto"),
    STR("updates.url", updates_url, "https://api.github.com/repos/JJLDonley/Gifu/releases?per_page=30"),
    STR("updates.last_check", updates_last_check, ""),
    STR("updates.skip", updates_skip, ""),
    STR("markup.colors", markup_colors, "e0503c,3c7be0,3cb371,e0b33c,9b59b6,7f8c8d"),
    BOOL("markup.count_empty", markup_count_empty, 0),
    STR("sync.capture", sync_capture, "auto"),
    STR("sync.capture_command", sync_capture_command, ""),
    INT("sync.interval_ms", sync_interval_ms, 400, 100, 10000),
    STR("sync.area", sync_area, ""),
    INT("tsumego.margin", tsumego_margin, 2, 1, 5),
    INT("report.compare_rank", report_compare_rank, 11, 0, 11),
    // Mistakes table: sorted by winrate | points | move; `by` picks which biggest
    // losses are listed when sorted by move number.
    STR("report.mistakes_sort", report_mistakes_sort, "winrate"),
    // Overview range: 0 whole game, 1-3 a phase, 4 moves report.range_from..range_to (0 = the end).
    INT("report.range", report_range, 0, 0, 4),
    INT("report.range_from", report_range_from, 1, 1, 9999),
    INT("report.range_to", report_range_to, 0, 0, 9999),
    STR("report.mistakes_by", report_mistakes_by, "winrate"),
    INT("engine.max_processes", engine_max_processes, 1, 1, 4),
    INT("new_game.size", new_game_size, 19, 5, 25),
    FLOAT("new_game.komi", new_game_komi, 7.5, -150.0, 150.0),
    INT("new_game.handicap", new_game_handicap, 0, 0, 9),
    STR("new_game.black", new_game_black, ""),
    STR("new_game.white", new_game_white, ""),
    STR("new_game.opponent", new_game_opponent, "none"),
    TSTR("game_info.black", game_info_black),
    TSTR("game_info.white", game_info_white),
    TSTR("game_info.black_rank", game_info_black_rank),
    TSTR("game_info.white_rank", game_info_white_rank),
    TFLOAT("game_info.komi", game_info_komi, 7.5, -150.0, 150.0),
    TSTR("game_info.result", game_info_result),
    TSTR("game_info.event", game_info_event),
    TSTR("game_info.date", game_info_date),
    INT("window.width", window_width, 1400, 640, 16384),
    INT("window.height", window_height, 900, 480, 16384),
};

#undef STR
#undef BOOL
#undef INT
#undef FLOAT
#undef TSTR
#undef TFLOAT

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
        if (!f && sv_eq(e->key, SV("updates.channel"))) continue;   // retired: saved by 0.1.0-alpha.1 without the user choosing
        if (!f && sv_eq(e->key, SV("board.show_move_numbers"))) {   // retired: on/off is now all / last N / none
            bool on = false;
            if (kv_parse_bool(e->value, &on) && on) config->move_numbers = -1;
            continue;
        }
        if (!f || f->transient) {
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
    String out = string_from(SV("# Gifu settings (written by the app; edit while it is closed).\n"
                                "# Keymap, engines, layout and themes overrides live next to this file:\n"
                                "# keymap.kv, engines.kv, layout.lisp, themes/*.theme\n"));
    StringView group = SV_NULL;
    for (isize i = 0; i < countof(FIELDS); ++i) {
        const ConfigField *f = &FIELDS[i];
        if (f->transient) continue;
        StringView key = sv(f->key);
        isize dot = sv_find_char(key, '.');
        StringView g = dot > 0 ? sv_take(key, dot) : SV("");
        if (!group.data || !sv_eq(g, group)) string_push_char(&out, '\n');
        group = g;
        switch (f->type) {
            case CONFIG_STRING: {
                string_pushf(&out, "%s = ", f->key);
                kv_write_value(&out, config_get_string(config, f));   // escaped: Windows paths keep their backslashes
                string_push_char(&out, '\n');
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
