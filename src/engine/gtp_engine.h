// gtp_engine.h - any GTP engine (e.g. Leela): it plays moves (genmove, with
// time_settings for the thinking time) and scores (final_score). Engines that know
// Leela Zero's lz-analyze (Leela as built with Gifu) also analyse: candidates with
// visits, win rates and lines, but no score (AnalysisSnapshot.no_score).
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
