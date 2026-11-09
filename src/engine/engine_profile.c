#include "engine/engine_profile.h"

#include "base/log.h"
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

static EngineProfile *profile_for(EngineProfileVec *out, StringView id) {
    vec_foreach(EngineProfile, p, out) {
        if (sv_eq(string_sv(&p->id), id)) return p;
    }
    vec_push(out, (EngineProfile){ .id = string_from(id), .name = string_from(id), .kind = string_from(SV("katago-gtp")),
                                   .program = string_from(SV("")), .model = string_from(SV("")), .config = string_from(SV("")),
                                   .working_dir = string_from(SV("")) });
    return &vec_last(out);
}

static void set_field(String *dst, String value) {
    string_free(dst);
    *dst = value;
}

void engine_profiles_from_kv(const KvFile *kv, const PathVars *vars, EngineProfileVec *out) {
    EngineProfileVec loaded = { 0 };
    // Args are replaced wholesale per file: remember which profiles got new args here.
    vec_foreach(KvEntry, e, &kv->entries) {
        isize dot = sv_find_char(e->key, '.');
        if (dot <= 0) continue;
        StringView id = sv_take(e->key, dot), field = sv_drop(e->key, dot + 1);
        EngineProfile *p = profile_for(&loaded, id);
        if (sv_eq(field, SV("name"))) set_field(&p->name, string_from(e->value));
        else if (sv_eq(field, SV("kind"))) set_field(&p->kind, string_from(e->value));
        else if (sv_eq(field, SV("program"))) set_field(&p->program, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("model"))) set_field(&p->model, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("config"))) set_field(&p->config, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("working_dir"))) set_field(&p->working_dir, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("arg"))) vec_push(&p->args, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("platforms"))) {
            StringView rest = e->value;
            while (rest.len) {
                StringView item = sv_trim(sv_chop_char(&rest, ','));
                if (item.len) vec_push(&p->platforms, string_from(item));
            }
        } else {
            log_warn("engines:%d: unknown field '" SV_FMT "'", e->line, SV_ARG(field));
        }
    }
    vec_foreach(EngineProfile, p, &loaded) {
        bool applies = p->platforms.len == 0;
        vec_foreach(String, plat, &p->platforms) applies |= sv_eq(string_sv(plat), sv(os_platform_name()));
        if (!applies) {
            profile_free(p);
            continue;
        }
        String model = p->model;
        p->model = resolve_model(model);
        // A later profile with the same id (user file) replaces an earlier one.
        bool replaced = false;
        for (isize k = 0; k < out->len && !replaced; ++k) {
            if (!sv_eq(string_sv(&out->items[k].id), string_sv(&p->id))) continue;
            profile_free(&out->items[k]);
            out->items[k] = *p;
            replaced = true;
        }
        if (!replaced) vec_push(out, *p);
    }
    vec_free(&loaded);
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
