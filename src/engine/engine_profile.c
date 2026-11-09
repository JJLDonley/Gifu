#include "engine/engine_profile.h"

#include "engine/katago_engine.h"

#include <string.h>

String path_vars_expand(const PathVars *vars, StringView text) {
    String out = { 0 };
    StringView rest = text;
    while (rest.len > 0) {
        isize start = sv_find(rest, SV("${"));
        if (start < 0) {
            string_push(&out, rest);
            break;
        }
        string_push(&out, sv_take(rest, start));
        rest = sv_drop(rest, start + 2);
        isize end = sv_find_char(rest, '}');
        if (end < 0) {
            string_push(&out, SV("${"));
            continue;
        }
        StringView name = sv_take(rest, end);
        rest = sv_drop(rest, end + 1);
        if (sv_eq(name, SV("root"))) string_push(&out, string_sv(&vars->root));
        else if (sv_eq(name, SV("exe_dir"))) string_push(&out, string_sv(&vars->exe_dir));
        else if (sv_eq(name, SV("platform"))) string_push_cstr(&out, os_platform_name());
#if defined(OS_WINDOWS)
        else if (sv_eq(name, SV("exe"))) string_push(&out, SV(".exe"));
#else
        else if (sv_eq(name, SV("exe"))) {
        }
#endif
    }
    return out;
}

// A directory model path resolves to the last *.bin.gz / *.bin / *.txt.gz in it.
static String resolve_model(String path) {
    if (!os_dir_exists(string_sv(&path))) return path;
    StringVec names = { 0 };
    os_list_dir(string_sv(&path), &names);
    String chosen = { 0 };
    for (isize i = names.len - 1; i >= 0; --i) {
        StringView n = string_sv(&names.items[i]);
        if (sv_ends_with(n, SV(".bin.gz")) || sv_ends_with(n, SV(".bin")) || sv_ends_with(n, SV(".txt.gz"))) {
            chosen = path_join(string_sv(&path), n);
            break;
        }
    }
    string_vec_free(&names);
    if (chosen.len == 0) return path;
    string_free(&path);
    return chosen;
}

static void read_string_array(JsonValue array, const PathVars *vars, StringVec *out) {
    for (isize i = 0; i < json_array_len(array); ++i) {
        vec_push(out, path_vars_expand(vars, json_string(json_array_at(array, i), SV(""))));
    }
}

static void profile_free(EngineProfile *p) {
    string_free(&p->id);
    string_free(&p->name);
    string_free(&p->kind);
    string_free(&p->program);
    string_free(&p->model);
    string_free(&p->config);
    string_free(&p->working_dir);
    string_vec_free(&p->args);
    string_vec_free(&p->platforms);
}

void engine_profiles_from_json(JsonValue array, const PathVars *vars, EngineProfileVec *out) {
    for (isize i = 0; i < json_array_len(array); ++i) {
        JsonValue item = json_array_at(array, i);
        EngineProfile p = { 0 };
        read_string_array(json_get(item, "platforms"), vars, &p.platforms);
        bool applies = p.platforms.len == 0;
        vec_foreach(String, plat, &p.platforms) applies |= sv_eq(string_sv(plat), sv(os_platform_name()));
        if (!applies) {
            string_vec_free(&p.platforms);
            continue;
        }
        p.id = string_from(json_string(json_get(item, "id"), SV("engine")));
        p.name = string_from(json_string(json_get(item, "name"), string_sv(&p.id)));
        p.kind = string_from(json_string(json_get(item, "kind"), SV("katago-gtp")));
        p.program = path_vars_expand(vars, json_string(json_get(item, "program"), SV("")));
        p.model = resolve_model(path_vars_expand(vars, json_string(json_get(item, "model"), SV(""))));
        p.config = path_vars_expand(vars, json_string(json_get(item, "config"), SV("")));
        p.working_dir = path_vars_expand(vars, json_string(json_get(item, "working_dir"), SV("")));
        read_string_array(json_get(item, "args"), vars, &p.args);
        // A later profile with the same id (user config) replaces an earlier one.
        bool replaced = false;
        for (isize k = 0; k < out->len && !replaced; ++k) {
            if (!sv_eq(string_sv(&out->items[k].id), string_sv(&p.id))) continue;
            profile_free(&out->items[k]);
            out->items[k] = p;
            replaced = true;
        }
        if (!replaced) vec_push(out, p);
    }
}

void engine_profiles_free(EngineProfileVec *profiles) {
    vec_foreach(EngineProfile, p, profiles) profile_free(p);
    vec_free(profiles);
}

const EngineProfile *engine_profiles_find(const EngineProfileVec *profiles, StringView id) {
    vec_foreach(EngineProfile, p, profiles) {
        if (sv_eq(string_sv(&p->id), id)) return p;
    }
    return NULL;
}

bool engine_profile_available(const EngineProfile *profile) {
    if (!os_file_exists(string_sv(&profile->program))) return false;
    return profile->model.len == 0 || os_file_exists(string_sv(&profile->model));
}

// ---- Factory ------------------------------------------------------------------------

typedef Engine *(*EngineConstructor)(const EngineProfile *profile, String *error);

static Engine *create_katago_gtp(const EngineProfile *p, String *error) {
    StringView extra[16];
    isize n = 0;
    vec_foreach(String, a, &p->args) {
        if (n < (isize)countof(extra)) extra[n++] = string_sv(a);
    }
    StringView working_dir = string_sv(&p->working_dir);
    if (working_dir.len == 0) working_dir = path_dirname(string_sv(&p->program));
    KataGoSettings settings = {
        .name = string_sv(&p->name),
        .program = string_sv(&p->program),
        .model = string_sv(&p->model),
        .config = string_sv(&p->config),
        .working_dir = working_dir,
        .extra_args = extra,
        .extra_arg_count = n,
    };
    return katago_engine_create(&settings, error);
}

static const struct {
    const char *kind;
    EngineConstructor create;
} ENGINE_KINDS[] = {
    { "katago-gtp", create_katago_gtp },
};

Engine *engine_create(const EngineProfile *profile, String *error) {
    for (i32 i = 0; i < (i32)countof(ENGINE_KINDS); ++i) {
        if (sv_eq(string_sv(&profile->kind), sv(ENGINE_KINDS[i].kind))) return ENGINE_KINDS[i].create(profile, error);
    }
    if (error) *error = string_format("unknown engine kind '" SV_FMT "'", SV_ARG(string_sv(&profile->kind)));
    return NULL;
}
