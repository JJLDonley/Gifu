// katago_engine.h - KataGo over GTP (`katago gtp ...`), implements Engine.
#pragma once

#include "engine/engine.h"

typedef struct KataGoSettings {
    StringView name;
    StringView program;
    StringView model;
    StringView config;
    StringView working_dir;
    StringView human_model;   // optional -human-model
    StringView human_profile; // initial humanSLProfile (used only with a human_model)
    StringView overrides;     // extra ",key=value" pairs (katago_tuning_overrides)
    const StringView *extra_args;
    isize extra_arg_count;
    const StringView *launcher;   // optional prefix (ssh ...)
    isize launcher_count;
} KataGoSettings;

Engine *katago_engine_create(const KataGoSettings *settings, String *error);
