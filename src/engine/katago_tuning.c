#include "engine/katago_tuning.h"

#include "base/log.h"

static bool is_value_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

bool katago_overrides_valid(StringView extra) {
    StringView rest = sv_trim(extra);
    while (rest.len > 0) {
        isize comma = sv_find_char(rest, ',');
        StringView pair = sv_trim(comma >= 0 ? sv_take(rest, comma) : rest);
        rest = comma >= 0 ? sv_drop(rest, comma + 1) : SV("");
        isize eq = sv_find_char(pair, '=');
        if (eq < 0) return false;
        StringView key = sv_trim(sv_take(pair, eq)), value = sv_trim(sv_drop(pair, eq + 1));
        if (key.len == 0 || value.len == 0) return false;
        for (isize i = 0; i < key.len; ++i) {
            if (!is_value_char(key.data[i])) return false;
        }
        for (isize i = 0; i < value.len; ++i) {
            if (!is_value_char(value.data[i])) return false;
        }
    }
    return true;
}

static StringView gpu_key(StringView program) {
    if (sv_find(program, SV("opencl")) >= 0) return SV("openclDeviceToUse");
    if (sv_find(program, SV("cuda")) >= 0) return SV("cudaDeviceToUse");
    if (sv_find(program, SV("trt")) >= 0 || sv_find(program, SV("tensorrt")) >= 0) return SV("trtDeviceToUse");
    return SV("");   // CPU (Eigen) builds, remote launchers without a hint
}

void katago_tuning_overrides(const KataGoTuning *t, bool analysis, StringView program, String *out) {
    if (!t) return;
    if (analysis) {
        if (t->analysis_threads > 0) string_pushf(out, ",numAnalysisThreads=%d", t->analysis_threads);
        if (t->analysis_search_threads > 0) string_pushf(out, ",numSearchThreadsPerAnalysisThread=%d", t->analysis_search_threads);
    } else {
        if (t->search_threads > 0) string_pushf(out, ",numSearchThreads=%d", t->search_threads);
        if (t->max_visits > 0) string_pushf(out, ",maxVisits=%d", t->max_visits);
    }
    StringView key = gpu_key(program);
    if (t->gpu >= 0 && key.len) string_pushf(out, "," SV_FMT "=%d", SV_ARG(key), t->gpu);
    if (t->nn_batch > 0) string_pushf(out, ",nnMaxBatchSize=%d", t->nn_batch);
    if (t->nn_cache_power > 0) string_pushf(out, ",nnCacheSizePowerOfTwo=%d", t->nn_cache_power);
    StringView extra = sv_trim(t->extra);
    if (extra.len) {
        if (katago_overrides_valid(extra)) {
            // Re-join without spaces.
            StringView rest = extra;
            while (rest.len > 0) {
                isize comma = sv_find_char(rest, ',');
                StringView pair = sv_trim(comma >= 0 ? sv_take(rest, comma) : rest);
                rest = comma >= 0 ? sv_drop(rest, comma + 1) : SV("");
                isize eq = sv_find_char(pair, '=');
                string_pushf(out, "," SV_FMT "=" SV_FMT, SV_ARG(sv_trim(sv_take(pair, eq))), SV_ARG(sv_trim(sv_drop(pair, eq + 1))));
            }
        } else {
            log_warn("katago.overrides ignored (expected key=value, key=value): " SV_FMT, SV_ARG(extra));
        }
    }
}
