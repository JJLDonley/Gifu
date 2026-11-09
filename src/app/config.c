#include "app/config.h"

#include "base/log.h"
#include "base/os.h"

#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

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

bool config_load(Config *config, String *error) {
    if (!os_file_exists(string_sv(&config->path))) return true;
    json_free(&config->doc);
    if (!json_load_file(string_sv(&config->path), &config->doc, error)) return false;
    JsonValue root = json_root(&config->doc);
    for (isize i = 0; i < countof(FIELDS); ++i) {
        const ConfigField *f = &FIELDS[i];
        JsonValue v = json_path(root, f->key);
        if (!json_exists(v)) continue;
        switch (f->type) {
            case CONFIG_STRING:
                if (json_is_string(v)) config_set_string(config, f, json_string(v, SV("")));
                break;
            case CONFIG_BOOL: config_set_bool(config, f, json_bool(v, config_get_bool(config, f))); break;
            case CONFIG_INT:
            case CONFIG_FLOAT: config_set_number(config, f, json_number(v, config_get_number(config, f))); break;
        }
    }
    return true;
}

JsonValue config_section(const Config *config, const char *key) {
    return json_get(json_root(&config->doc), key);
}

// Returns (creating if needed) the object for the dotted prefix of `key`, and
// writes the last segment's name into `leaf`.
static yyjson_mut_val *object_for_key(yyjson_mut_doc *doc, yyjson_mut_val *root, const char *key, char *leaf, usize leaf_cap) {
    yyjson_mut_val *obj = root;
    StringView rest = sv(key);
    for (;;) {
        StringView part = sv_chop_char(&rest, '.');
        if (rest.len == 0) {
            snprintf(leaf, leaf_cap, SV_FMT, SV_ARG(part));
            return obj;
        }
        yyjson_mut_val *child = yyjson_mut_obj_getn(obj, part.data, (usize)part.len);
        if (!child) {
            child = yyjson_mut_obj(doc);
            yyjson_mut_obj_add(obj, yyjson_mut_strncpy(doc, part.data, (usize)part.len), child);
        }
        obj = child;
    }
}

bool config_save(const Config *config) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    for (isize i = 0; i < countof(FIELDS); ++i) {
        const ConfigField *f = &FIELDS[i];
        char leaf[64];
        yyjson_mut_val *obj = object_for_key(doc, root, f->key, leaf, sizeof(leaf));
        yyjson_mut_val *key = yyjson_mut_strcpy(doc, leaf);
        yyjson_mut_val *val = NULL;
        switch (f->type) {
            case CONFIG_STRING: {
                StringView s = config_get_string(config, f);
                val = yyjson_mut_strncpy(doc, s.data, (usize)s.len);
                break;
            }
            case CONFIG_BOOL: val = yyjson_mut_bool(doc, config_get_bool(config, f)); break;
            case CONFIG_INT: val = yyjson_mut_sint(doc, (i64)config_get_number(config, f)); break;
            case CONFIG_FLOAT: val = yyjson_mut_real(doc, round(config_get_number(config, f) * 1000.0) / 1000.0); break;
        }
        yyjson_mut_obj_add(obj, key, val);
    }

    // Preserve user override sections verbatim.
    static const char *const PRESERVED[] = { "engines", "keymap", "layout" };
    for (i32 i = 0; i < (i32)countof(PRESERVED); ++i) {
        JsonValue section = config_section(config, PRESERVED[i]);
        if (!json_exists(section)) continue;
        yyjson_mut_obj_add_val(doc, root, PRESERVED[i], yyjson_val_mut_copy(doc, section.val));
    }

    usize len = 0;
    char *text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
    yyjson_mut_doc_free(doc);
    if (!text) return false;
    os_make_dirs(path_dirname(string_sv(&config->path)));
    bool ok = os_write_file(string_sv(&config->path), sv_from_parts(text, (isize)len));
    free(text);
    if (!ok) log_warn("could not write %s", string_cstr(&config->path));
    return ok;
}

void config_free(Config *config) {
    for (isize i = 0; i < countof(FIELDS); ++i) {
        if (FIELDS[i].type == CONFIG_STRING) string_free((String *)field_ptr(config, &FIELDS[i]));
    }
    string_free(&config->path);
    json_free(&config->doc);
}
