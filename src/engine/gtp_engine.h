// gtp_engine.h - any GTP engine as an opponent (e.g. the original Leela): it
// plays moves (genmove, with time_settings for the thinking time) and scores
// (final_score); it does not analyse, so analysis panels stay with KataGo.
#pragma once

#include "engine/engine.h"

typedef struct GtpEngineSettings {
    StringView name;
    StringView program;
    StringView working_dir;
    const StringView *args;       // e.g. --gtp --noponder
    isize arg_count;
    const StringView *launcher;   // optional prefix (ssh ...)
    isize launcher_count;
} GtpEngineSettings;

Engine *gtp_engine_create(const GtpEngineSettings *settings, String *error);
