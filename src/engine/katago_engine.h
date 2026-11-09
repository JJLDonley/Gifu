// katago_engine.h - KataGo over GTP (`katago gtp ...`), implements Engine.
#pragma once

#include "engine/engine.h"

typedef struct KataGoSettings {
    StringView name;
    StringView program;
    StringView model;
    StringView config;
    StringView working_dir;
    const StringView *extra_args;
    isize extra_arg_count;
} KataGoSettings;

Engine *katago_engine_create(const KataGoSettings *settings, String *error);
