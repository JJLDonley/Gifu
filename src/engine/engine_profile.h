// engine_profile.h - data-driven engine definitions and the engine factory.
//
// assets/data/engines.kv, then <config_dir>/engines.kv. Keys are
// "<profile-id>.<field>"; profiles keep their first-appearance order:
//   katago-opencl.name     = KataGo · OpenCL
//   katago-opencl.kind     = katago-gtp
//   katago-opencl.program  = ${root}/katago/${platform}-opencl/katago${exe}
//   katago-opencl.model    = ${root}/katago/networks
//   katago-opencl.config   = ...
//   katago-opencl.analysis_config = ...           (enables fast whole-game analysis)
//   katago-opencl.human_model = ${root}/katago/networks/human  (human SL network,
//                                                   loaded when present: -human-model; the
//                                                   strength is humanSLProfile from config)
//   katago-human.human_required = true              (unavailable without it)
//   katago-opencl.arg      = -override-config     (repeat for each argument)
//   katago-opencl.platforms = linux, windows        (optional)
//   remote.launcher = ssh -T user@gpu-box           (space-separated prefix; program,
//                                                   model and config are remote paths)
//
// Strings may contain variables: ${root} (install root), ${exe_dir},
// ${platform} (windows|linux|macos) and ${exe} (".exe" on Windows, else "").
// A `model` that names a directory picks the most recently installed network inside it.
#pragma once

#include "base/kv.h"
#include "base/os.h"
#include "engine/batch_analyzer.h"
#include "engine/engine.h"

typedef struct EngineProfile {
    String id;
    String name;
    String kind;        // "katago-gtp"
    String program;
    String model;
    String config;
    String analysis_config;   // enables the JSON batch analyzer for this engine
    String human_model;       // human SL network (empty = none / not installed)
    bool human_required;      // profile is unusable without its human network (human-style play)
    String working_dir;
    StringVec args;
    StringVec launcher;   // e.g. ssh -T user@host: the engine runs remotely
    StringVec platforms;   // empty = all
} EngineProfile;

typedef Vec(EngineProfile) EngineProfileVec;

typedef struct PathVars {
    String root;
    String exe_dir;
} PathVars;

String path_vars_expand(const PathVars *vars, StringView text);

// Adds/overrides profiles from a key=value file (only those for this platform).
void engine_profiles_from_kv(const KvFile *kv, const PathVars *vars, EngineProfileVec *out);
void engine_profiles_free(EngineProfileVec *profiles);
const EngineProfile *engine_profiles_find(const EngineProfileVec *profiles, StringView id);
// True when the program (and model, if any) exist on disk.
bool engine_profile_available(const EngineProfile *profile);

// Factory: maps profile.kind to a concrete Engine implementation. `human_profile`
// (e.g. "rank_5k", "preaz_1d", "proyear_1990") sets humanSLProfile when the
// profile has a human_model; ignored otherwise.
Engine *engine_create(const EngineProfile *profile, StringView human_profile, String *error);
// Batch analyzer for the profile (NULL with an error when it has no analysis_config).
BatchAnalyzer *batch_analyzer_create(const EngineProfile *profile, String *error);
bool engine_profile_can_batch(const EngineProfile *profile);
// First available profile with a human network (preferring `preferred`), or NULL.
const EngineProfile *engine_profiles_find_human(const EngineProfileVec *profiles, StringView preferred);
