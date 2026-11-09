// engine_profile.h - data-driven engine definitions (assets/data/engines.json,
// overridable from the user's config) and the engine factory.
//
// Strings may contain variables: ${root} (install root), ${exe_dir},
// ${platform} (windows|linux|macos) and ${exe} (".exe" on Windows, else "").
// A `model` that names a directory picks the newest-named *.bin.gz inside it.
#pragma once

#include "base/json.h"
#include "base/os.h"
#include "engine/engine.h"

typedef struct EngineProfile {
    String id;
    String name;
    String kind;        // "katago-gtp"
    String program;
    String model;
    String config;
    String working_dir;
    StringVec args;
    StringVec platforms;   // empty = all
} EngineProfile;

typedef Vec(EngineProfile) EngineProfileVec;

typedef struct PathVars {
    String root;
    String exe_dir;
} PathVars;

String path_vars_expand(const PathVars *vars, StringView text);

// Appends the profiles of `array` (a JSON array) that apply to this platform.
void engine_profiles_from_json(JsonValue array, const PathVars *vars, EngineProfileVec *out);
void engine_profiles_free(EngineProfileVec *profiles);
const EngineProfile *engine_profiles_find(const EngineProfileVec *profiles, StringView id);
// True when the program (and model, if any) exist on disk.
bool engine_profile_available(const EngineProfile *profile);

// Factory: maps profile.kind to a concrete Engine implementation.
Engine *engine_create(const EngineProfile *profile, String *error);
