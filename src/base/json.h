// json.h - small read-only facade over yyjson for loading data files.
// Data files may contain // comments and trailing commas.
#pragma once

#include "base/base.h"
#include "base/str.h"

typedef struct JsonDoc {
    void *doc;   // yyjson_doc*
} JsonDoc;

typedef struct JsonValue {
    void *val;   // yyjson_val*, NULL when missing
} JsonValue;

typedef struct JsonObjectIter {
    u8 storage[48];   // yyjson_obj_iter
} JsonObjectIter;

NODISCARD bool json_parse(StringView text, JsonDoc *out, String *error);
NODISCARD bool json_load_file(StringView path, JsonDoc *out, String *error);
void json_free(JsonDoc *doc);

JsonValue json_root(const JsonDoc *doc);
JsonValue json_get(JsonValue object, const char *key);
// Dotted path lookup: json_path(v, "engine.model").
JsonValue json_path(JsonValue object, const char *dotted);

bool json_exists(JsonValue v);
bool json_is_object(JsonValue v);
bool json_is_array(JsonValue v);
bool json_is_string(JsonValue v);
bool json_is_number(JsonValue v);

StringView json_string(JsonValue v, StringView fallback);
f64 json_number(JsonValue v, f64 fallback);
i64 json_int(JsonValue v, i64 fallback);
bool json_bool(JsonValue v, bool fallback);

isize json_array_len(JsonValue v);
JsonValue json_array_at(JsonValue v, isize index);

void json_object_iter_init(JsonValue object, JsonObjectIter *it);
bool json_object_iter_next(JsonObjectIter *it, StringView *key, JsonValue *value);
