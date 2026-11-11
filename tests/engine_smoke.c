// Engine smoke test: starts the first available KataGo profile, syncs a short
// position, analyzes it for a few seconds and checks we get candidates.
// Label "engine" (excluded from the default test preset; needs the network).
#include "base/kv.h"
#include "base/log.h"
#include "platform/os.h"
#include "core/sgf.h"
#include "engine/engine_profile.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    log_set_level(LOG_LEVEL_INFO);
    String engines_path = path_join(SV(GIFU_SOURCE_DIR), SV("assets/data/engines.kv"));
    KvFile kv = { 0 };
    String err = { 0 };
    if (!kv_load(&kv, string_sv(&engines_path))) {
        fprintf(stderr, "cannot read %s\n", string_cstr(&engines_path));
        return 2;
    }
    PathVars vars = { string_from(SV(GIFU_SOURCE_DIR)), os_executable_dir() };
    EngineProfileVec profiles = { 0 };
    engine_profiles_from_kv(&kv, &vars, &profiles);
    // Extra profiles (e.g. a launcher/remote setup) from GIFU_ENGINES_EXTRA.
    const char *extra = getenv("GIFU_ENGINES_EXTRA");
    KvFile extra_kv = { 0 };
    if (extra && kv_load(&extra_kv, sv(extra))) engine_profiles_from_kv(&extra_kv, &vars, &profiles);
    kv_free(&extra_kv);

    const EngineProfile *profile = NULL;
    StringView wanted = argc > 1 ? sv(argv[1]) : SV("");
    vec_foreach(EngineProfile, p, &profiles) {
        bool match = wanted.len ? sv_eq(string_sv(&p->id), wanted) : true;
        if (match && engine_profile_available(p) && !profile) profile = p;
    }
    if (!profile) {
        printf("SKIP: no available engine profile\n");
        return 0;
    }
    printf("engine: %s\n  program %s\n  model   %s\n", string_cstr(&profile->name), string_cstr(&profile->program),
           string_cstr(&profile->model));

    Engine *engine = engine_create(profile, SV("rank_5k"), NULL, &err);
    if (!engine) {
        fprintf(stderr, "create failed: %s\n", string_cstr(&err));
        return 1;
    }

    SgfError sgf_err;
    GameTree *tree = sgf_parse(SV("(;SZ[19]KM[7.5];B[pd];W[dp];B[pp];W[dd])"), &sgf_err);
    game_tree_to_end(tree);
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    position_spec_from_node(tree, tree->current, SV("chinese"), spec);
    engine_set_position(engine, spec);
    engine_start_analysis(engine, &(AnalysisRequest){ .interval_centiseconds = 20, .include_ownership = true });

    const char *limit = getenv("GIFU_ENGINE_TIMEOUT");
    f64 deadline = os_time_seconds() + (limit ? atof(limit) : 900.0);
    i32 updates = 0, best_visits = 0;
    bool ready_logged = false;
    while (os_time_seconds() < deadline && best_visits < 400) {
        EngineEvent ev;
        while (engine_poll(engine, &ev)) {
            if (ev.kind == ENGINE_EVENT_LOG) printf("  log: " SV_FMT "\n", SV_ARG(ev.text));
            if (ev.kind == ENGINE_EVENT_ERROR) printf("  error: " SV_FMT "\n", SV_ARG(ev.text));
            if (ev.kind == ENGINE_EVENT_STATUS) {
                printf("  status: %s\n", engine_status_name(ev.status));
                if (ev.status == ENGINE_STATUS_FAILED) deadline = 0;
            }
            if (ev.kind == ENGINE_EVENT_ANALYSIS && ev.analysis->candidate_count > 0) {
                updates++;
                const Candidate *best = analysis_best(ev.analysis);
                best_visits = ev.analysis->total_visits;
                if (!ready_logged || updates % 10 == 0) {
                    char coord[8];
                    point_to_gtp(best->move, 19, 19, coord);
                    printf("  analysis #%d: best %s winrate %.3f score %.2f visits %d (ownership %d)\n", updates, coord,
                           best->winrate, best->score_lead, ev.analysis->total_visits, ev.analysis->has_ownership);
                    ready_logged = true;
                }
            }
        }
        os_sleep_ms(10);
    }
    // Region analysis: only moves inside the lower-left 7x7 corner (A1:G7).
    bool region_ok = false;
    {
        AnalysisRequest req = { .interval_centiseconds = 20, .region = { true, 0, 12, 6, 18 } };
        engine_start_analysis(engine, &req);
        i32 checked = 0, outside = 0;
        for (f64 until = os_time_seconds() + 120.0; os_time_seconds() < until && checked < 20;) {
            EngineEvent ev;
            while (engine_poll(engine, &ev)) {
                if (ev.kind == ENGINE_EVENT_ERROR) printf("  error: " SV_FMT "\n", SV_ARG(ev.text));
                if (ev.kind != ENGINE_EVENT_ANALYSIS || ev.analysis->candidate_count == 0) continue;
                checked++;
                for (i32 i = 0; i < ev.analysis->candidate_count; ++i) {
                    Point p = ev.analysis->candidates[i].move;
                    if (!point_is_pass(p) && (p.x > 6 || p.y < 12)) outside++;
                }
            }
            os_sleep_ms(10);
        }
        region_ok = checked > 0 && outside == 0;
        printf("  region analysis: %d updates, %d candidates outside A1:G7\n", checked, outside);
        engine_stop_analysis(engine);
    }

    // Scoring: final_score on the same position.
    bool scored = false;
    engine_request_score(engine, spec->id);
    for (f64 until = os_time_seconds() + 60.0; os_time_seconds() < until && !scored;) {
        EngineEvent ev;
        while (engine_poll(engine, &ev)) {
            if (ev.kind == ENGINE_EVENT_SCORE) {
                printf("  final_score: " SV_FMT "\n", SV_ARG(ev.text));
                scored = ev.text.len > 0 && ev.position_id == spec->id;
            }
        }
        os_sleep_ms(10);
    }
    // Human SL policy (profiles with a human network only).
    bool human_ok = profile->human_model.len == 0;
    if (!human_ok) {
        engine_request_human_policy(engine, spec->id);
        for (f64 until = os_time_seconds() + 60.0; os_time_seconds() < until && !human_ok;) {
            EngineEvent ev;
            while (engine_poll(engine, &ev)) {
                if (ev.kind == ENGINE_EVENT_ERROR) printf("  error: " SV_FMT "\n", SV_ARG(ev.text));
                if (ev.kind != ENGINE_EVENT_HUMAN_POLICY) continue;
                const HumanPolicy *h = ev.human_policy;
                f32 sum = h->pass, best = 0;
                i32 best_i = 0;
                for (i32 i = 0; i < h->width * h->height; ++i) {
                    sum += h->policy[i];
                    if (h->policy[i] > best) best = h->policy[i], best_i = i;
                }
                char coord[8] = "";
                point_to_gtp((Point){ (i8)(best_i % h->width), (i8)(best_i / h->width) }, h->width, h->height, coord);
                printf("  human policy: top %s %.1f%%, sum %.3f\n", coord, best * 100.0f, sum);
                human_ok = ev.position_id == spec->id && sum > 0.95f && sum < 1.05f;
            }
            os_sleep_ms(10);
        }
    }
    engine_destroy(engine);
    free(spec);
    game_tree_free(tree);
    engine_profiles_free(&profiles);
    kv_free(&kv);
    bool pass = updates > 0 && scored && region_ok && human_ok;
    printf("%s: %d analysis updates, %d visits, scored=%d, region=%d, human=%d\n", pass ? "PASS" : "FAIL", updates, best_visits, scored,
           region_ok, human_ok);
    return pass ? 0 : 1;
}
