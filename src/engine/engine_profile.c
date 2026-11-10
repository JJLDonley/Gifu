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
// A model directory resolves to the most recently installed network in it
// (ties: the last name), so a freshly downloaded network takes over.
static String resolve_model(String path) {
    if (!os_dir_exists(string_sv(&path))) return path;
    StringVec names = { 0 };
    os_list_dir(string_sv(&path), &names);
    String chosen = { 0 };
    i64 chosen_mtime = 0;
    for (isize i = 0; i < names.len; ++i) {
        StringView n = string_sv(&names.items[i]);
        if (!sv_ends_with(n, SV(".bin.gz")) && !sv_ends_with(n, SV(".bin")) && !sv_ends_with(n, SV(".txt.gz"))) continue;
        String candidate = path_join(string_sv(&path), n);
        i64 mtime = os_file_mtime(string_sv(&candidate));
        if (chosen.len == 0 || mtime >= chosen_mtime) {
            string_free(&chosen);
            chosen = candidate;
            chosen_mtime = mtime;
        } else {
            string_free(&candidate);
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
    string_free(&p->analysis_config);
    string_free(&p->human_model);
    string_vec_free(&p->args);
    string_vec_free(&p->launcher);
    string_vec_free(&p->platforms);
}

static EngineProfile *profile_for(EngineProfileVec *out, StringView id) {
    vec_foreach(EngineProfile, p, out) {
        if (sv_eq(string_sv(&p->id), id)) return p;
    }
    vec_push(out, (EngineProfile){ .id = string_from(id), .name = string_from(id), .kind = string_from(SV("katago-gtp")),
                                   .program = string_from(SV("")), .model = string_from(SV("")), .config = string_from(SV("")),
                                   .working_dir = string_from(SV("")), .analysis_config = string_from(SV("")),
                                   .human_model = string_from(SV("")) });
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
        else if (sv_eq(field, SV("analysis_config"))) set_field(&p->analysis_config, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("human_model"))) set_field(&p->human_model, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("human_required"))) p->human_required = sv_eq(e->value, SV("true")) || sv_eq(e->value, SV("1"));
        else if (sv_eq(field, SV("working_dir"))) set_field(&p->working_dir, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("arg"))) vec_push(&p->args, path_vars_expand(vars, e->value));
        else if (sv_eq(field, SV("launcher"))) {
            string_vec_free(&p->launcher);
            StringView rest = e->value;
            for (StringView tok = sv_chop_token(&rest); tok.len; tok = sv_chop_token(&rest)) vec_push(&p->launcher, path_vars_expand(vars, tok));
        }
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
        String human = p->human_model;
        p->human_model = resolve_model(human);
        // Optional: a local human network that is not installed (yet) is simply not loaded.
        if (p->human_model.len && !p->launcher.len && !os_file_exists(string_sv(&p->human_model))) string_clear(&p->human_model);
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
    if (profile->launcher.len) return true;   // remote: files live on the other machine
    if (!os_file_exists(string_sv(&profile->program))) return false;
    if (profile->human_required && profile->human_model.len == 0) return false;
    return profile->model.len == 0 || os_file_exists(string_sv(&profile->model));
}

// ---- Factory ------------------------------------------------------------------------

typedef Engine *(*EngineConstructor)(const EngineProfile *profile, StringView human_profile, const KataGoTuning *tuning, String *error);


// KataGoSettings for a profile; `storage` holds the arrays the settings point into.
typedef struct SettingsStorage {
    StringView extra[16];
    StringView launcher[16];
} SettingsStorage;

static KataGoSettings settings_for(const EngineProfile *p, SettingsStorage *st) {
    isize n = 0, launcher_count = 0;
    vec_foreach(String, a, &p->args) {
        if (n < (isize)countof(st->extra)) st->extra[n++] = string_sv(a);
    }
    vec_foreach(String, l, &p->launcher) {
        if (launcher_count < (isize)countof(st->launcher)) st->launcher[launcher_count++] = string_sv(l);
    }
    StringView working_dir = string_sv(&p->working_dir);
    if (working_dir.len == 0 && !p->launcher.len) working_dir = path_dirname(string_sv(&p->program));
    return (KataGoSettings){
        .launcher = st->launcher,
        .launcher_count = launcher_count,
        .name = string_sv(&p->name),
        .program = string_sv(&p->program),
        .model = string_sv(&p->model),
        .config = string_sv(&p->config),
        .working_dir = working_dir,
        .human_model = string_sv(&p->human_model),
        .extra_args = st->extra,
        .extra_arg_count = n,
    };
}

const EngineProfile *engine_profiles_find_human(const EngineProfileVec *profiles, StringView preferred) {
    const EngineProfile *p = preferred.len ? engine_profiles_find(profiles, preferred) : NULL;
    if (p && p->human_model.len && engine_profile_available(p)) return p;
    vec_foreach(EngineProfile, it, profiles) {
        if (it->human_model.len && engine_profile_available(it)) return it;
    }
    return NULL;
}

static Engine *create_katago_gtp(const EngineProfile *p, StringView human_profile, const KataGoTuning *tuning, String *error) {
    SettingsStorage st;
    KataGoSettings settings = settings_for(p, &st);
    settings.human_profile = human_profile;
    String overrides = { 0 };
    katago_tuning_overrides(tuning, false, string_sv(&p->program), &overrides);
    settings.overrides = string_sv(&overrides);
    Engine *engine = katago_engine_create(&settings, error);
    string_free(&overrides);
    return engine;
}

static const struct {
    const char *kind;
    EngineConstructor create;
} ENGINE_KINDS[] = {
    { "katago-gtp", create_katago_gtp },
};

Engine *engine_create(const EngineProfile *profile, StringView human_profile, const KataGoTuning *tuning, String *error) {
    for (i32 i = 0; i < (i32)countof(ENGINE_KINDS); ++i) {
        if (sv_eq(string_sv(&profile->kind), sv(ENGINE_KINDS[i].kind))) return ENGINE_KINDS[i].create(profile, human_profile, tuning, error);
    }
    if (error) *error = string_format("unknown engine kind '" SV_FMT "'", SV_ARG(string_sv(&profile->kind)));
    return NULL;
}

bool engine_profile_can_batch(const EngineProfile *profile) {
    return profile && sv_eq(string_sv(&profile->kind), SV("katago-gtp")) && profile->analysis_config.len &&
           (profile->launcher.len || os_file_exists(string_sv(&profile->analysis_config)));
}

BatchAnalyzer *batch_analyzer_create(const EngineProfile *profile, const KataGoTuning *tuning, String *error) {
    if (!engine_profile_can_batch(profile)) {
        if (error) *error = string_from(SV("engine profile has no usable analysis_config"));
        return NULL;
    }
    StringView working_dir = string_sv(&profile->working_dir);
    if (working_dir.len == 0 && !profile->launcher.len) working_dir = path_dirname(string_sv(&profile->program));
    StringView launcher[16];
    isize launcher_count = 0;
    vec_foreach(String, l, &profile->launcher) {
        if (launcher_count < (isize)countof(launcher)) launcher[launcher_count++] = string_sv(l);
    }
    KataGoAnalyzerSettings settings = {
        .launcher = launcher,
        .launcher_count = launcher_count,
        .program = string_sv(&profile->program),
        .model = string_sv(&profile->model),
        .config = string_sv(&profile->analysis_config),
        .working_dir = working_dir,
    };
    String overrides = { 0 };
    katago_tuning_overrides(tuning, true, string_sv(&profile->program), &overrides);
    settings.overrides = string_sv(&overrides);
    BatchAnalyzer *analyzer = katago_analyzer_create(&settings, error);
    string_free(&overrides);
    return analyzer;
}
