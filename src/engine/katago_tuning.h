// katago_tuning.h - user-adjustable KataGo settings (Settings -> KataGo), sent as
// -override-config entries on top of the profile's .cfg file.
//
// 0 (or -1 for the GPU) means "keep the .cfg value". `extra` is free-form
// "key=value, key=value" for anything else; only safe characters are passed on.
#pragma once

#include "base/str.h"

typedef struct KataGoTuning {
    i32 search_threads;            // numSearchThreads (GTP engine)
    i32 max_visits;                // maxVisits (engine moves; live analysis is unbounded)
    i32 gpu;                       // openclDeviceToUse / cudaDeviceToUse / trtDeviceToUse; -1 = auto
    i32 nn_batch;                  // nnMaxBatchSize
    i32 nn_cache_power;            // nnCacheSizePowerOfTwo
    i32 analysis_threads;          // numAnalysisThreads (JSON game analysis)
    i32 analysis_search_threads;   // numSearchThreadsPerAnalysisThread
    StringView extra;              // "key=value, ..." (user)
} KataGoTuning;

// Appends ",key=value" pairs for the GTP engine (analysis = false) or the JSON
// analyzer (analysis = true). `program` picks the GPU key from the backend in
// its path (opencl / cuda / trt; CPU builds get none).
void katago_tuning_overrides(const KataGoTuning *tuning, bool analysis, StringView program, String *out);
// True if `extra` only holds key=value pairs of [A-Za-z0-9_.-] separated by commas.
bool katago_overrides_valid(StringView extra);
