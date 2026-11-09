#include "base/json.h"

#include "base/os.h"

#include <string.h>
#include <yyjson.h>

_Static_assert(sizeof(yyjson_obj_iter) <= sizeof(((JsonObjectIter *)0)->storage), "grow JsonObjectIter storage");

#define READ_FLAGS (YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS)

bool json_parse(StringView text, JsonDoc *out, String *error) {
    yyjson_read_err err;
    yyjson_doc *doc = yyjson_read_opts((char *)text.data, (usize)text.len, READ_FLAGS, NULL, &err);
    if (!doc) {
        if (error) *error = string_format("JSON error at byte %zu: %s", err.pos, err.msg);
        return false;
    }
    out->doc = doc;
    return true;
}

bool json_load_file(StringView path, JsonDoc *out, String *error) {
    String text = { 0 };
    if (!os_read_file(path, &text)) {
        if (error) *error = string_format("cannot read " SV_FMT, SV_ARG(path));
        return false;
    }
    bool ok = json_parse(string_sv(&text), out, error);
    if (!ok && error) {
        String detailed = string_format(SV_FMT ": %s", SV_ARG(path), string_cstr(error));
        string_free(error);
        *error = detailed;
    }
    string_free(&text);
    return ok;
}

void json_free(JsonDoc *doc) {
    if (doc->doc) yyjson_doc_free(doc->doc);
    doc->doc = NULL;
}

JsonValue json_root(const JsonDoc *doc) {
    return (JsonValue){ doc->doc ? yyjson_doc_get_root(doc->doc) : NULL };
}

JsonValue json_get(JsonValue object, const char *key) {
    if (!object.val || !yyjson_is_obj(object.val)) return (JsonValue){ 0 };
    return (JsonValue){ yyjson_obj_get(object.val, key) };
}

JsonValue json_path(JsonValue object, const char *dotted) {
    StringView rest = sv(dotted);
    JsonValue current = object;
    while (rest.len > 0 && current.val) {
        StringView key = sv_chop_char(&rest, '.');
        if (!yyjson_is_obj(current.val)) return (JsonValue){ 0 };
        current.val = yyjson_obj_getn(current.val, key.data, (usize)key.len);
    }
    return current;
}

bool json_exists(JsonValue v) { return v.val != NULL; }
bool json_is_object(JsonValue v) { return v.val && yyjson_is_obj(v.val); }
bool json_is_array(JsonValue v) { return v.val && yyjson_is_arr(v.val); }
bool json_is_string(JsonValue v) { return v.val && yyjson_is_str(v.val); }
bool json_is_number(JsonValue v) { return v.val && yyjson_is_num(v.val); }

StringView json_string(JsonValue v, StringView fallback) {
    if (!json_is_string(v)) return fallback;
    return (StringView){ yyjson_get_str(v.val), (isize)yyjson_get_len(v.val) };
}

f64 json_number(JsonValue v, f64 fallback) {
    return json_is_number(v) ? yyjson_get_num(v.val) : fallback;
}

i64 json_int(JsonValue v, i64 fallback) {
    if (!json_is_number(v)) return fallback;
    return yyjson_is_int(v.val) ? yyjson_get_sint(v.val) : (i64)yyjson_get_num(v.val);
}

bool json_bool(JsonValue v, bool fallback) {
    return v.val && yyjson_is_bool(v.val) ? yyjson_get_bool(v.val) : fallback;
}

isize json_array_len(JsonValue v) {
    return json_is_array(v) ? (isize)yyjson_arr_size(v.val) : 0;
}

JsonValue json_array_at(JsonValue v, isize index) {
    if (!json_is_array(v)) return (JsonValue){ 0 };
    return (JsonValue){ yyjson_arr_get(v.val, (usize)index) };
}

void json_object_iter_init(JsonValue object, JsonObjectIter *it) {
    memset(it, 0, sizeof(*it));
    if (json_is_object(object)) yyjson_obj_iter_init(object.val, (yyjson_obj_iter *)it->storage);
}

bool json_object_iter_next(JsonObjectIter *it, StringView *key, JsonValue *value) {
    yyjson_obj_iter *iter = (yyjson_obj_iter *)it->storage;
    if (!iter->obj) return false;
    yyjson_val *k = yyjson_obj_iter_next(iter);
    if (!k) return false;
    *key = (StringView){ yyjson_get_str(k), (isize)yyjson_get_len(k) };
    value->val = yyjson_obj_iter_get_val(k);
    return true;
}
