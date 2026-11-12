#include "app/app.h"
#include "app/ai_style.h"
#include "app/updater.h"

#include "platform/file_dialog.h"
#include "core/sgf_analysis.h"
#include "core/tsumego_frame.h"
#include "platform/line_channel.h"
#include "base/log.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---- Helpers ----------------------------------------------------------------------------

StringView app_tr(const App *app, const char *key, const char *fallback) {
    return i18n_get(&app->i18n, sv(key), sv(fallback));
}

void app_set_status(App *app, const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    string_free(&app->status_message);
    app->status_message = string_from(sv(buffer));
    app->status_until = os_time_seconds() + 4.0;
    log_info("%s", buffer);
}

void app_log(App *app, StringView line) {
    log_debug("engine: " SV_FMT, SV_ARG(line));   // --verbose mirrors the engine log
    // KataGo's one-time OpenCL tuning takes minutes with no other sign of life.
    if (sv_find(line, SV("Beginning GPU tuning")) >= 0) {
        app_set_status(app, "KataGo is tuning itself for this GPU (first start with this network, a few minutes)");
    } else if (sv_find(line, SV("Done tuning")) >= 0) {
        app_set_status(app, "GPU tuning finished");
    }
    i32 slot = (app->log_head + app->log_count) % APP_LOG_LINES;
    if (app->log_count == APP_LOG_LINES) {
        slot = app->log_head;
        app->log_head = (app->log_head + 1) % APP_LOG_LINES;
    } else {
        app->log_count++;
    }
    string_free(&app->log_lines[slot]);
    app->log_lines[slot] = string_from(line);
}

static String find_root_dir(StringView exe_dir) {
    String dir = string_from(exe_dir);
    for (i32 depth = 0; depth < 6; ++depth) {
        String katago = path_join(string_sv(&dir), SV("katago"));
        bool found = os_dir_exists(string_sv(&katago));
        string_free(&katago);
        if (found) return dir;
        StringView parent = path_dirname(string_sv(&dir));
        if (parent.len == 0 || parent.len >= dir.len) break;
        String next = string_from(parent);
        string_free(&dir);
        dir = next;
    }
    string_free(&dir);
    return string_from(exe_dir);
}

// Loads assets/data/<name> then <config_dir>/<name> (user overrides) into `kv`.
static void load_kv_layers(const App *app, const char *name, KvFile *kv) {
    String rel = string_format("data/%s", name);
    String shipped = path_join(string_sv(&app->assets_dir), string_sv(&rel));
    String user = path_join(string_sv(&app->config_dir), sv(name));
    if (!kv_load(kv, string_sv(&shipped))) log_error("cannot read %s", string_cstr(&shipped));
    if (kv_load(kv, string_sv(&user))) log_info("user overrides: %s", string_cstr(&user));
    string_free(&rel);
    string_free(&shipped);
    string_free(&user);
}

// ---- Engine -----------------------------------------------------------------------------

AnalysisRequest app_analysis_request(const App *app) {
    return (AnalysisRequest){
        .interval_centiseconds = app->config.analysis_interval_cs,
        .include_ownership = app->config.show_ownership || app->ownership_wanted,
        .max_candidates = app->config.max_candidates,
        .region = app->region,
    };
}

void app_set_region(App *app, BoardRegion region) {
    if (region.active) {   // normalise to x0 <= x1, y0 <= y1
        i8 x0 = MIN(region.x0, region.x1), x1 = MAX(region.x0, region.x1);
        i8 y0 = MIN(region.y0, region.y1), y1 = MAX(region.y0, region.y1);
        region = (BoardRegion){ true, x0, y0, x1, y1 };
    }
    app->region = region;
    app->analysis_valid = false;
    if (app->analysis_enabled) app_set_analysis(app, true);   // re-issue kata-analyze with/without allow
    if (app->second.engine) {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(app->second.engine, &req);
    }
    if (region.active) app_set_status(app, "Analysis limited to the selected region");
}

// The engines' humanSLProfile (the Human moves overlay and its cache key, rank
// estimates restore it): analysis.human_profile.
StringView app_human_profile(App *app) {
    snprintf(app->human_profile_effective, sizeof(app->human_profile_effective), "%s", string_cstr(&app->config.analysis_human_profile));
    return sv(app->human_profile_effective);
}

// The AI's human-like play: ai.human_profile, with White at ai.human_white_rank when
// set. KataGo switches to it for its own move only.
StringView app_ai_human_profile(App *app) {
    human_profile_combine(string_sv(&app->config.human_profile), string_sv(&app->config.human_white_rank), app->ai_human_profile_effective,
                          (isize)sizeof(app->ai_human_profile_effective));
    return sv(app->ai_human_profile_effective);
}

KataGoTuning app_katago_tuning(const App *app) {
    const Config *c = &app->config;
    return (KataGoTuning){
        .search_threads = c->katago_search_threads,
        .max_visits = c->katago_max_visits,
        .gpu = c->katago_gpu,
        .nn_batch = c->katago_nn_batch,
        .nn_cache_power = c->katago_nn_cache_power,
        .analysis_threads = c->katago_analysis_threads,
        .analysis_search_threads = c->katago_analysis_search_threads,
        .extra = string_sv(&c->katago_overrides),
    };
}

static void stop_engine(App *app) {
    if (app->rank_slot == &app->main || app->rank_batch) app_rank_stop(app);   // the analyzer follows the main profile
    app_batch_stop(app);
    engine_destroy(app->main.engine);
    app->main.engine = NULL;
    app->main.status = ENGINE_STATUS_STOPPED;
    app->main.synced_node = NULL;
    app->main.waiting_move = false;
    app->main.human_requested = 0;
    app->score_requested = false;
    app->score_pending = false;
    app->analysis_valid = false;
}

static void start_engine(App *app) {
    stop_engine(app);
    string_free(&app->main.error);
    StringView wanted = app->options.engine.len ? app->options.engine : string_sv(&app->config.engine);
    if (sv_eq(wanted, SV("none"))) return;
    const EngineProfile *profile = wanted.len ? engine_profiles_find(&app->engine_profiles, wanted) : NULL;
    if (profile && !engine_profile_available(profile)) {
        log_warn("engine profile '" SV_FMT "' is not available", SV_ARG(wanted));
        profile = NULL;
    }
    if (!profile) {
        // The first installed analysis engine (KataGo); a playing-only engine (kind gtp,
        // e.g. Leela) is the main engine only when chosen explicitly.
        vec_foreach(EngineProfile, p, &app->engine_profiles) {
            if (engine_profile_available(p) && sv_eq(string_sv(&p->kind), SV("katago-gtp"))) {
                profile = p;
                break;
            }
        }
    }
    app->main.profile = profile;
    if (!profile) {
        app->main.error = string_from(SV("no engine found (see assets/data/engines.kv)"));
        return;
    }
    String err = { 0 };
    KataGoTuning tuning = app_katago_tuning(app);
    app->main.engine = engine_create(profile, app_human_profile(app), &tuning, &err);
    if (!app->main.engine) {
        app->main.status = ENGINE_STATUS_FAILED;
        app->main.error = err;
        log_error("engine: %s", string_cstr(&err));
        return;
    }
    app->main.status = ENGINE_STATUS_STARTING;
    if (app->analysis_enabled) {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(app->main.engine, &req);
    }
}

void app_restart_engine(App *app) {
    if (app->rank_batch) app_rank_stop(app);
    app_batch_stop(app);
    batch_analyzer_destroy(app->analyzer);   // may belong to the previous profile
    app->analyzer = NULL;
    start_engine(app);
}

void app_set_analysis(App *app, bool enabled) {
    app->analysis_enabled = enabled;
    if (!app->main.engine) return;
    if (enabled) {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(app->main.engine, &req);
    } else {
        engine_stop_analysis(app->main.engine);
    }
}

static void store_node_eval(GameTree *tree, GameNode *node, const AnalysisSnapshot *a) {
    const Candidate *best = analysis_best(a);
    if (!best) return;
    NodeEval e = {
        .valid = true,
        .black_winrate = analysis_black_winrate(a, best->winrate),
        .black_score = analysis_black_score(a, best->score_lead),
        .visits = a->total_visits,
    };
    for (i32 i = 0; i < a->candidate_count && e.top_count < NODE_EVAL_TOP; ++i) {
        e.top_share[e.top_count] = a->total_visits > 0 ? (f32)a->candidates[i].visits / (f32)a->total_visits : 0.0f;
        e.top[e.top_count++] = a->candidates[i].move;
    }
    game_node_update_eval(tree, node, &e);   // keeps a deeper stored evaluation
}

// ---- Whole-game analysis ------------------------------------------------------------------

bool app_game_analysis_running(const App *app) { return app->batch_running || app->flash_running; }

void app_game_analysis_progress(const App *app, isize *done, isize *total) {
    if (app->flash_running) {
        *done = app->flash_job.received;
        *total = app->flash_job.turns;
    } else {
        *done = app->batch_index;
        *total = app->batch_count;
    }
}

// Releases the analysis engine's GPU memory (KataGo processes are expensive)
// once neither the game analysis nor a rank estimate uses it.
static void analyzer_release(App *app) {
    if (app->flash_running || app->rank_batch) return;
    batch_analyzer_destroy(app->analyzer);
    app->analyzer = NULL;
}

// Starts the JSON analyzer for the main profile if it is not running. Fails if
// the profile has no analysis config, or the process would not fit next to the
// running engines (engine.max_processes).
static bool analyzer_ensure(App *app) {
    if (app->analyzer && !batch_analyzer_alive(app->analyzer)) {
        batch_analyzer_destroy(app->analyzer);
        app->analyzer = NULL;
    }
    if (app->analyzer) return true;
    if (!engine_profile_can_batch(app->main.profile)) return false;
    i32 running = (app->main.engine != NULL) + (app->second.engine != NULL);
    if (running + 1 > app->config.engine_max_processes) return false;
    String err = { 0 };
    KataGoTuning tuning = app_katago_tuning(app);
    app->analyzer = batch_analyzer_create(app->main.profile, &tuning, &err);
    if (!app->analyzer) {
        log_warn("analysis engine: %s (falling back to GTP)", string_cstr(&err));
        string_free(&err);
        return false;
    }
    return true;
}

static bool rank_batch_begin(App *app, const PositionSpec *spec, GameNode *end);

static void flash_finish(App *app) {
    app->flash_running = false;
    game_job_free(&app->flash_job);
    analyzer_release(app);
    if (app->batch_restore_analysis) app_set_analysis(app, true);   // resume interactive analysis
}

static void flash_stop(App *app) {
    if (!app->flash_running) return;
    if (app->analyzer) batch_analyzer_cancel(app->analyzer, string_sv(&app->flash_job.id));
    flash_finish(app);
}

// Whole game in one query to the JSON analyzer. Returns false if unavailable, or
// if it would not fit next to the running engines (engine.max_processes): the
// game is then analysed over GTP by the main engine, which keeps running.
static bool flash_start(App *app) {
    if (!analyzer_ensure(app)) return false;
    GameNode *end = app->tree->current;
    while (end->first_child) end = end->first_child;   // end of the active line
    char id[32];
    snprintf(id, sizeof(id), "flash%u", ++app->flash_serial);
    game_job_build(&app->flash_job, app->tree, end, sv(id), string_sv(&app->config.rules));
    AnalysisJob request = game_job_request(&app->flash_job, app->config.analysis_game_visits);
    if (!batch_analyzer_submit(app->analyzer, &request)) {
        game_job_free(&app->flash_job);
        analyzer_release(app);
        return false;
    }
    app->flash_running = true;
    // With the human network loaded, the same engine scores the ranks too (CGR[]):
    // 1-visit policy queries, batched alongside the search.
    if (app->main.profile->human_model.len && !app->rank_job) rank_batch_begin(app, app->flash_job.spec, end);
    // Give the GPU to the batch: pause interactive analysis until it finishes.
    app->batch_restore_analysis = app->analysis_enabled;
    app_set_analysis(app, false);
    app_set_status(app, "Analyzing game (%d positions, %d visits each)", app->flash_job.turns, app->config.analysis_game_visits);
    return true;
}

// Routes the JSON analyzer's results to the game analysis and the rank estimate.
static void analyzer_update(App *app) {
    if (!app->analyzer) return;
    TurnResult r;
    while (app->analyzer && batch_analyzer_poll(app->analyzer, &r)) {
        if (app->rank_batch && rank_batch_apply(app->rank_batch, &r)) {
            turn_result_free(&r);
            continue;
        }
        bool done = app->flash_running && game_job_apply(&app->flash_job, &r) && game_job_done(&app->flash_job);
        turn_result_free(&r);
        if (done) {
            if (app->flash_job.failed) app_set_status(app, "Game analysis failed: %s", string_cstr(&app->flash_job.error));
            else app_set_status(app, "Game analysis complete");
            flash_finish(app);   // destroys the analyzer
        }
    }
    if (app->analyzer && !batch_analyzer_alive(app->analyzer)) {
        if (app->flash_running || app->rank_batch) app_set_status(app, "Analysis engine exited");
        flash_stop(app);
        app_rank_stop(app);
        analyzer_release(app);
    }
}

void app_batch_stop(App *app) {
    flash_stop(app);
    if (!app->batch_running) return;
    app->batch_running = false;
    free(app->batch_nodes);
    app->batch_nodes = NULL;
    app->batch_count = app->batch_index = 0;
    app->main.synced_node = NULL;   // go back to following the current node
    if (!app->batch_restore_analysis) app_set_analysis(app, false);
}

void app_batch_start(App *app) {
    if (!app->main.engine) return;
    app_batch_stop(app);
    if (flash_start(app)) return;   // fast path: KataGo JSON analysis
    enum { MAX_LINE = 4096 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    if (!line) abort();
    // Analyse the whole active line: root..current..end of the main continuation.
    isize count = MIN(game_tree_active_line(app->tree, line, MAX_LINE), (isize)MAX_LINE);
    app->batch_nodes = line;
    app->batch_count = count;
    app->batch_index = 0;
    // Skip nodes that already have enough visits (e.g. loaded from CGE[]).
    while (app->batch_index < count && line[app->batch_index]->eval.valid &&
           line[app->batch_index]->eval.visits >= app->config.analysis_game_visits) {
        app->batch_index++;
    }
    app->batch_restore_analysis = app->analysis_enabled;
    app->batch_running = true;
    app->ai_plays[STONE_BLACK] = app->ai_plays[STONE_WHITE] = false;
    app->main.synced_node = NULL;
    app_set_analysis(app, true);
    app_set_status(app, "Analyzing game (%td positions, %d visits each)", count, app->config.analysis_game_visits);
}

static void batch_advance(App *app) {
    app->batch_index++;
    while (app->batch_index < app->batch_count && app->batch_nodes[app->batch_index]->eval.visits >= app->config.analysis_game_visits) {
        app->batch_index++;
    }
    if (app->batch_index >= app->batch_count) {
        app_set_status(app, "Game analysis complete");
        app_batch_stop(app);
    }
}

// ---- Second engine (comparison / engine-vs-engine) ----------------------------------------

static void slot_clear(EngineSlot *slot) {
    engine_destroy(slot->engine);
    slot->engine = NULL;
    slot->status = ENGINE_STATUS_STOPPED;
    slot->synced_node = NULL;
    slot->analysis_valid = false;
    slot->waiting_move = false;
    slot->human_requested = 0;
}

static bool slot_has_human(const EngineSlot *s);

void app_second_engine(App *app, bool on) {
    if (on && app->analyzer) {   // the JSON analyzer is running
        app_set_status(app, "Second engine: wait for the game analysis to finish");
        return;
    }
    if (on && app->config.engine_max_processes < 2) {
        app_set_status(app, "Second engine needs engine.max_processes = 2 (most GPUs run one KataGo at a time)");
        return;
    }
    EngineSlot *s = &app->second;
    if (app->rank_slot == s) app_rank_stop(app);
    slot_clear(s);
    string_free(&s->error);
    if (!on) {
        app->match_mode = false;
        return;
    }
    StringView wanted = string_sv(&app->config.engine2);
    const EngineProfile *p = wanted.len ? engine_profiles_find(&app->engine_profiles, wanted) : app->main.profile;
    if (!p || !engine_profile_available(p)) {
        s->error = string_from(SV("second engine profile is not available"));
        app_set_status(app, "Second engine: profile not available");
        return;
    }
    String err = { 0 };
    KataGoTuning tuning = app_katago_tuning(app);
    s->engine = engine_create(p, app_human_profile(app), &tuning, &err);
    s->profile = p;
    if (!s->engine) {
        s->status = ENGINE_STATUS_FAILED;
        s->error = err;
        return;
    }
    if (!s->analysis) s->analysis = calloc(1, sizeof(AnalysisSnapshot));
    if (!s->analysis) abort();
    s->status = ENGINE_STATUS_STARTING;
    AnalysisRequest req = app_analysis_request(app);
    engine_start_analysis(s->engine, &req);
    app_set_status(app, "Second engine: %s", string_cstr(&p->name));
}

static bool slot_has_human(const EngineSlot *s) {
    return s->engine && s->profile && s->profile->human_model.len && s->status == ENGINE_STATUS_READY;
}

// The engine that answers human-policy requests: main if it has a human model, else the second engine.
static EngineSlot *human_slot(App *app) {
    if (slot_has_human(&app->main)) return &app->main;
    if (slot_has_human(&app->second)) return &app->second;
    return NULL;
}

bool app_has_human_engine(const App *app) { return human_slot((App *)app) != NULL; }

static void on_human_policy(App *app, EngineSlot *s, const EngineEvent *ev) {
    if (ev->position_id != s->synced_position_id || !s->synced_node) return;
    memcpy(app->human_policy, ev->human_policy, sizeof(HumanPolicy));
    app->human_policy_node = s->synced_node;
    snprintf(app->human_policy_profile, sizeof(app->human_policy_profile), "%s", s->human_requested_profile);
}

// The overlay's policy belongs to a (position, human profile) pair: a different
// strength simply makes it not match, nothing has to clear it.
bool app_human_policy_for(const App *app, const GameNode *node) {
    char profile[48];
    human_profile_combine(string_sv(&app->config.human_profile), string_sv(&app->config.human_white_rank), profile, (isize)sizeof(profile));
    return app->human_policy_node == node && sv_eq(sv(app->human_policy_profile), sv(profile));
}

static void human_update(App *app) {
    EngineSlot *s = app->config.show_human_policy && !app->rank_job ? human_slot(app) : NULL;
    if (!s || s->synced_node != app->tree->current || app_human_policy_for(app, app->tree->current)) return;
    StringView profile = app_human_profile(app);
    if (s->human_requested == s->synced_position_id && sv_eq(sv(s->human_requested_profile), profile)) return;   // asked already
    s->human_requested = s->synced_position_id;
    snprintf(s->human_requested_profile, sizeof(s->human_requested_profile), SV_FMT, SV_ARG(profile));
    engine_request_human_policy(s->engine, s->synced_position_id);
}

void app_rank_stop(App *app) {
    if (app->rank_batch) {
        if (app->analyzer) rank_batch_cancel(app->rank_batch, app->analyzer);
        rank_batch_destroy(app->rank_batch);
        app->rank_batch = NULL;
        analyzer_release(app);
    }
    if (!app->rank_job) return;
    rank_estimator_destroy(app->rank_job);
    app->rank_job = NULL;
    // Hand the engine back: resync its position and resume analysis.
    EngineSlot *slot = app->rank_slot;
    app->rank_slot = NULL;
    if (!slot || !slot->engine) return;
    slot->synced_node = NULL;
    slot->human_requested = 0;
    engine_set_human_profile(slot->engine, app_human_profile(app));   // in case it changed meanwhile
    if (slot == &app->main) {
        if (app->analysis_enabled) app_set_analysis(app, true);
    } else {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(slot->engine, &req);
    }
}

// A new estimate replaces the old one: its moves fill in as they are scored.
static void clear_line_ranks(App *app, GameNode *end) {
    for (GameNode *n = end; n; n = n->parent) {
        if (game_node_find_prop(n, SV(RANK_SGF_PROP))) game_node_remove_prop(app->tree, n, SV(RANK_SGF_PROP));
    }
}

// Scores the line ending at `end` on the JSON analyzer (must be running).
static bool rank_batch_begin(App *app, const PositionSpec *spec, GameNode *end) {
    char id[32];
    snprintf(id, sizeof(id), "rank%u", ++app->flash_serial);
    app->rank_batch = rank_batch_create(spec, sv(id));
    if (!rank_batch_submit(app->rank_batch, app->analyzer)) {
        rank_batch_destroy(app->rank_batch);
        app->rank_batch = NULL;
        return false;
    }
    app->rank_end = end;
    app->rank_stored = 0;
    clear_line_ranks(app, end);
    return true;
}

// Fast path: the main profile has the human network and an analysis config,
// and its analyzer is running or fits in the process budget.
static bool rank_start_batched(App *app) {
    if (!app->main.profile || !app->main.profile->human_model.len || !analyzer_ensure(app)) return false;
    enum { MAX_LINE = 4096 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    if (!line || !spec) abort();
    isize n = MIN(game_tree_active_line(app->tree, line, MAX_LINE), (isize)MAX_LINE);
    position_spec_from_node(app->tree, line[n - 1], string_sv(&app->config.rules), spec);
    bool ok = rank_batch_begin(app, spec, line[n - 1]);
    if (ok) app_set_status(app, "Estimating ranks with %s (analysis engine)", string_cstr(&app->main.profile->name));
    else analyzer_release(app);
    free(spec);
    free(line);
    return ok;
}

void app_rank_start(App *app) {
    app_rank_stop(app);
    app->rank_pending = false;
    if (rank_start_batched(app)) return;
    EngineSlot *slot = human_slot(app);   // runs inside an engine that is already up (GPU budget)
    if (!slot) {
        bool starting = (app->main.engine && app->main.status == ENGINE_STATUS_STARTING && app->main.profile->human_model.len) ||
                        (app->second.engine && app->second.status == ENGINE_STATUS_STARTING && app->second.profile->human_model.len);
        if (starting) {
            app->rank_pending = true;
            app_set_status(app, "Rank estimate starts when the engine is ready");
        } else {
            app_set_status(app, app->human_available ? "Rank estimate needs an engine with the human network (Settings -> Engine)"
                                                     : "Rank estimate needs the human SL network (Engine -> Download engines...)");
        }
        return;
    }
    enum { MAX_LINE = 4096 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    if (!line || !spec) abort();
    isize n = MIN(game_tree_active_line(app->tree, line, MAX_LINE), (isize)MAX_LINE);
    position_spec_from_node(app->tree, line[n - 1], string_sv(&app->config.rules), spec);
    app_batch_stop(app);
    slot->waiting_move = false;
    app->rank_job = rank_estimator_start(slot->engine, spec, app_human_profile(app));
    app->rank_slot = slot;
    app->rank_end = line[n - 1];
    app->rank_stored = 0;
    clear_line_ranks(app, app->rank_end);
    app_set_status(app, "Estimating ranks with %s", string_cstr(&slot->profile->name));
    free(spec);
    free(line);
}

bool app_rank_progress(const App *app, i32 *done, i32 *total) {
    if (!app->rank_job && !app->rank_batch) return false;
    const RankProgress *p = app->rank_batch ? rank_batch_progress(app->rank_batch) : rank_estimator_progress(app->rank_job);
    *done = p->done;
    *total = p->total;
    return true;
}

static GameNode *active_line_end(const GameTree *tree) {
    GameNode *end = tree->current;
    while (end->first_child) end = end->first_child;
    return end;
}

// The rank summary is derived from the CGR[] of the active line. It is cached
// on (tree, revision, end of the active line): any change to the tree (a scored
// move, a loaded game, an edit, another variation) recomputes it; nothing has
// to remember to refresh it.
static void rank_sync(App *app) {
    GameNode *end = active_line_end(app->tree);
    if (app->rank_tree == app->tree && app->rank_revision == app->tree->revision && app->rank_line_end == end) return;
    app->rank_tree = app->tree;
    app->rank_revision = app->tree->revision;
    app->rank_line_end = end;
    memset(&app->rank, 0, sizeof(app->rank));
    app->rank_valid = false;
    enum { MAX_LINE = 4096 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    if (!line) abort();
    isize n = MIN(game_tree_active_line(app->tree, line, MAX_LINE), (isize)MAX_LINE);
    i32 points = game_tree_width(app->tree) * game_tree_height(app->tree);
    for (isize i = 0; i < n; ++i) {
        const GameNode *node = line[i];
        StringView text = node->has_move ? game_node_prop(node, SV(RANK_SGF_PROP)) : SV("");
        f32 p[RANK_COUNT];
        if (!text.len || !rank_probs_parse(text, p)) continue;
        rank_summary_add(&app->rank, node->move.color, node->move_number, points, p);
        app->rank_valid = true;
    }
    free(line);
}

static void rank_update(App *app) {
    if (app->rank_pending && human_slot(app)) app_rank_start(app);
    if (app->rank_job || app->rank_batch) {
        if (app->rank_job) rank_estimator_update(app->rank_job);
        const RankProgress *p = app->rank_batch ? rank_batch_progress(app->rank_batch) : rank_estimator_progress(app->rank_job);
        // Every scored move goes into the tree right away (CGR[]): the sidebar,
        // the graph and the Deep report all follow the job as it runs.
        if (p->done != app->rank_stored || p->finished) {
            app->rank_stored = p->done;
            rank_progress_store(p, app->tree, app->rank_end);
        }
        if (p->finished) {
            if (p->failed) {
                app_set_status(app, "Rank estimate failed: %s", string_cstr(&p->error));
            } else {
                app_set_status(app, "Estimated ranks: Black " SV_FMT ", White " SV_FMT, SV_ARG(rank_estimate_label(rank_tally_best(&p->tally[STONE_BLACK]))),
                               SV_ARG(rank_estimate_label(rank_tally_best(&p->tally[STONE_WHITE]))));
            }
            app_rank_stop(app);
        }
    }
    rank_sync(app);
}

// An engine resigned: the game is over. Every engine stops playing and the result
// (B+R / W+R) goes into the game unless it already has one.
static void engine_resigned(App *app, Stone loser, StringView who) {
    app->ai_plays[STONE_BLACK] = app->ai_plays[STONE_WHITE] = false;
    app->ai_move_once = false;
    app->match_mode = false;
    if (!game_tree_info(app->tree, SV("RE")).len) {
        game_node_set_prop(app->tree, app->tree->root, SV("RE"), loser == STONE_WHITE ? SV("B+R") : SV("W+R"));
        app_mark_edited(app);
    }
    app_set_status(app, SV_FMT " resigns: %s wins", SV_ARG(who), loser == STONE_WHITE ? "Black" : "White");
}

// Plays an engine's move. A move for the side that is not to move, or one our
// board refuses, means the engine's board differs: it is resynced (the next
// set_position rebuilds it if it refuses a command).
static void play_engine_move(App *app, EngineSlot *s, const EngineEvent *ev, const char *what) {
    Stone to_play = app->tree->current->to_play;
    MoveError err = ev->move.color == to_play ? app_play(app, ev->move.point) : MOVE_ERR_OCCUPIED;
    if (err == MOVE_OK) return;
    char coord[8];
    point_to_gtp(ev->move.point, game_tree_width(app->tree), game_tree_height(app->tree), coord);
    log_warn("%s %s %s refused (%s to play, %s): resyncing the engine", what, ev->move.color == STONE_BLACK ? "B" : "W", coord,
             to_play == STONE_BLACK ? "Black" : "White", ev->move.color == to_play ? move_error_name(err) : "wrong colour");
    app_set_status(app, "%s move %s refused (%s): resyncing the engine", what, coord, ev->move.color == to_play ? move_error_name(err) : "wrong colour");
    s->synced_node = NULL;
}

// Hidden analysis while an AI plays (see App.background_analysis). Not while the
// engine is lent out (rank estimate) or analysing a whole game.
static void background_analysis_update(App *app) {
    bool playing = app->ai_plays[STONE_BLACK] || app->ai_plays[STONE_WHITE];
    bool want = playing && !app->analysis_enabled && app->main.engine && app->main.status == ENGINE_STATUS_READY &&
                app->rank_slot != &app->main && !app->batch_running;
    if (want == app->background_analysis) return;
    app->background_analysis = want;
    if (want) {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(app->main.engine, &req);
    } else if (!app->analysis_enabled && app->main.engine) {
        engine_stop_analysis(app->main.engine);
    }
}

static void second_update(App *app) {
    EngineSlot *s = &app->second;
    if (!s->engine || app->rank_slot == s) return;   // lent to the rank job
    EngineEvent ev;
    while (engine_poll(s->engine, &ev)) {
        switch (ev.kind) {
            case ENGINE_EVENT_STATUS:
                s->status = ev.status;
                if (ev.status == ENGINE_STATUS_FAILED) app->match_mode = false;
                break;
            case ENGINE_EVENT_ANALYSIS:
                if (ev.analysis->position_id == s->synced_position_id) {
                    memcpy(s->analysis, ev.analysis, sizeof(AnalysisSnapshot));
                    s->analysis_valid = s->synced_node == app->tree->current;
                }
                break;
            case ENGINE_EVENT_MOVE:
                s->waiting_move = false;
                if (ev.position_id != s->synced_position_id || s->synced_node != app->tree->current) {
                    s->synced_node = NULL;
                    break;
                }
                if (ev.resigned) engine_resigned(app, ev.move.color, ev.move.color == STONE_BLACK ? SV("Black") : SV("White"));
                else play_engine_move(app, s, &ev, "Second engine");
                break;
            case ENGINE_EVENT_LOG: {
                String line = string_format("[2] " SV_FMT, SV_ARG(ev.text));
                app_log(app, string_sv(&line));
                string_free(&line);
                break;
            }
            case ENGINE_EVENT_HUMAN_POLICY:
                on_human_policy(app, s, &ev);
                break;
            case ENGINE_EVENT_ERROR:
            case ENGINE_EVENT_SCORE:
                break;
        }
    }
    if (s->synced_node != app->tree->current && s->status != ENGINE_STATUS_FAILED) {
        PositionSpec *spec = malloc(sizeof(PositionSpec));
        if (!spec) abort();
        position_spec_from_node(app->tree, app->tree->current, string_sv(&app->config.rules), spec);
        engine_set_position(s->engine, spec);
        s->synced_position_id = spec->id;
        s->synced_node = app->tree->current;
        s->analysis_valid = false;
        free(spec);
    }
}

// ---- Opponent engine (ai.engine) ------------------------------------------------------------

// The profile that should play against the user, or NULL when the main engine plays.
static const EngineProfile *opponent_profile(const App *app) {
    StringView id = string_sv(&app->config.ai_engine);
    if (!id.len || (app->main.profile && sv_eq(id, string_sv(&app->main.profile->id)))) return NULL;
    return engine_profiles_find(&app->engine_profiles, id);
}

// The engine that plays the AI's moves: the opponent engine (ai.engine) when one is
// chosen, else the main one. NULL when no AI plays.
const EngineSlot *app_playing_slot(const App *app) {
    if (!app->ai_plays[STONE_BLACK] && !app->ai_plays[STONE_WHITE]) return NULL;
    return opponent_profile(app) ? &app->opponent : &app->main;
}

static void opponent_start(App *app) {
    EngineSlot *s = &app->opponent;
    const EngineProfile *p = opponent_profile(app);
    if (s->engine && s->profile == p) return;
    slot_clear(s);
    string_free(&s->error);
    if (!p) return;
    if (!engine_profile_available(p)) {
        s->status = ENGINE_STATUS_FAILED;
        s->error = string_format("%s is not installed", string_cstr(&p->name));
        app_set_status(app, "Opponent: %s is not installed (Engine -> Download engines)", string_cstr(&p->name));
        return;
    }
    String err = { 0 };
    KataGoTuning tuning = app_katago_tuning(app);
    s->engine = engine_create(p, app_ai_human_profile(app), &tuning, &err);
    s->profile = p;
    if (!s->engine) {
        s->status = ENGINE_STATUS_FAILED;
        s->error = err;
        app_set_status(app, "Opponent: %s", string_cstr(&err));
        return;
    }
    s->status = ENGINE_STATUS_STARTING;
    app_set_status(app, "Opponent: %s", string_cstr(&p->name));
}

static void opponent_update(App *app) {
    EngineSlot *s = &app->opponent;
    bool wanted = (app->ai_plays[STONE_BLACK] || app->ai_plays[STONE_WHITE] || app->ai_move_once) && opponent_profile(app);
    if (wanted && s->status != ENGINE_STATUS_FAILED) opponent_start(app);
    if (!s->engine) return;
    EngineEvent ev;
    while (engine_poll(s->engine, &ev)) {
        switch (ev.kind) {
            case ENGINE_EVENT_STATUS: s->status = ev.status; break;
            case ENGINE_EVENT_MOVE:
                s->waiting_move = false;
                if (ev.position_id != s->synced_position_id || s->synced_node != app->tree->current) {
                    s->synced_node = NULL;
                    break;
                }
                if (ev.resigned) engine_resigned(app, app->tree->current->to_play, string_sv(&s->profile->name));
                else play_engine_move(app, s, &ev, string_cstr(&s->profile->name));
                break;
            case ENGINE_EVENT_LOG: {
                String line = string_format("[opponent] " SV_FMT, SV_ARG(ev.text));
                app_log(app, string_sv(&line));
                string_free(&line);
                break;
            }
            case ENGINE_EVENT_ERROR:
                s->waiting_move = false;
                app_set_status(app, "Opponent: " SV_FMT, SV_ARG(ev.text));
                break;
            default: break;
        }
    }
    if (s->synced_node != app->tree->current && s->status != ENGINE_STATUS_FAILED) {
        PositionSpec *spec = malloc(sizeof(PositionSpec));
        if (!spec) abort();
        position_spec_from_node(app->tree, app->tree->current, string_sv(&app->config.rules), spec);
        engine_set_position(s->engine, spec);
        s->synced_position_id = spec->id;
        s->synced_node = app->tree->current;
        free(spec);
    }
}

// ---- Profile ---------------------------------------------------------------------------------

void app_profile_build(App *app) {
    if (app->profile_builder) return;   // already building
    if (!app->config.profile_names.len || !app->config.profile_folder.len) {
        app_set_status(app, "Profile: set your player names and games folder in Settings -> Profile");
        return;
    }
    // The analysis runs its own KataGo: free the GPU when it cannot hold two.
    if (app->main.engine && app->config.engine_max_processes < 2) {
        stop_engine(app);
        app->profile_paused_engine = true;
    }
    String program = path_join(string_sv(&app->exe_dir), updater_program_name());
    String cache = path_join(string_sv(&app->config_dir), SV("profile"));
    ProfileBuildSpec spec = { string_sv(&app->config.profile_folder), string_sv(&app->config.profile_names), string_sv(&program),
                              string_sv(&cache), app->config.analysis_game_visits };
    app->profile_builder = profile_builder_start(&spec);
    string_free(&program);
    string_free(&cache);
}

void app_profile_cancel(App *app) {
    if (app->profile_builder) profile_builder_cancel(app->profile_builder);
}

ProfileStatus app_profile_status(const App *app) {
    if (!app->profile_builder) return (ProfileStatus){ .phase = app->profile_ready ? PROFILE_DONE : PROFILE_IDLE };
    return profile_builder_status(app->profile_builder);
}

void app_update_profile_now(App *app) {
    if (!app->profile_builder) return;
    ProfileStatus st = profile_builder_status(app->profile_builder);
    if (st.phase != PROFILE_DONE && st.phase != PROFILE_FAILED) return;
    ProfileData data;
    if (profile_builder_take(app->profile_builder, &data)) {
        profile_data_free(&app->profile);
        app->profile = data;
        app->profile_ready = true;
        app_set_status(app, "Profile: %td games added up", data.games.len);
    } else if (st.phase == PROFILE_FAILED) {
        app_set_status(app, "Profile: %s", st.error);
    }
    profile_builder_destroy(app->profile_builder);
    app->profile_builder = NULL;
    if (app->profile_paused_engine) {
        app->profile_paused_engine = false;
        start_engine(app);
    }
}

static void autosave_update(App *app);

void app_update(App *app) {
    if (app->retired_tree) {   // replaced last frame: nothing draws it any more
        game_tree_free(app->retired_tree);
        app->retired_tree = NULL;
    }
    autosave_update(app);
    app_sync_update(app);
    app_updates_update(app);
    analyzer_update(app);
    rank_update(app);
    second_update(app);
    opponent_update(app);
    app_update_profile_now(app);
    if (!app->main.engine || app->rank_slot == &app->main) return;   // lent to the rank job
    EngineEvent ev;
    while (engine_poll(app->main.engine, &ev)) {
        switch (ev.kind) {
            case ENGINE_EVENT_STATUS:
                app->main.status = ev.status;
                if (ev.status == ENGINE_STATUS_READY) app_set_status(app, "%s ready", string_cstr(&app->main.profile->name));
                if (ev.status == ENGINE_STATUS_FAILED) {
                    string_free(&app->main.error);
                    app->main.error = string_from(SV("engine exited (see engine log)"));
                }
                break;
            case ENGINE_EVENT_ANALYSIS:
                if (ev.analysis->position_id != app->main.synced_position_id || !app->main.synced_node) break;
                store_node_eval(app->tree, app->main.synced_node, ev.analysis);
                if (app->main.synced_node == app->tree->current) {
                    memcpy(app->analysis, ev.analysis, sizeof(AnalysisSnapshot));
                    app->analysis_valid = true;
                }
                if (app->batch_running && app->batch_index < app->batch_count &&
                    app->main.synced_node == app->batch_nodes[app->batch_index] &&
                    ev.analysis->total_visits >= app->config.analysis_game_visits) {
                    batch_advance(app);
                }
                break;
            case ENGINE_EVENT_MOVE:
                app->main.waiting_move = false;
                if (ev.position_id != app->main.synced_position_id) {
                    app->main.synced_node = NULL;   // stale answer: engine board must be resynced
                    break;
                }
                if (ev.resigned) {
                    engine_resigned(app, ev.move.color, string_sv(&app->main.profile->name));
                    break;
                }
                play_engine_move(app, &app->main, &ev, string_cstr(&app->main.profile->name));
                break;
            case ENGINE_EVENT_SCORE:
                app->score_requested = false;
                string_free(&app->result);
                app->result = string_from(ev.text);
                app->result_position = ev.position_id;
                app_set_status(app, "Result: " SV_FMT, SV_ARG(ev.text));
                break;
            case ENGINE_EVENT_HUMAN_POLICY:
                on_human_policy(app, &app->main, &ev);
                break;
            case ENGINE_EVENT_LOG:
                app_log(app, ev.text);
                break;
            case ENGINE_EVENT_ERROR:
                app_log(app, ev.text);
                log_warn("engine error: " SV_FMT, SV_ARG(ev.text));
                break;
        }
    }
    // The engine follows the current node, or the batch position while a
    // whole-game analysis runs.
    GameNode *target = app->batch_running && app->batch_index < app->batch_count ? app->batch_nodes[app->batch_index]
                                                                                   : app->tree->current;
    if (app->main.synced_node != target && app->main.status != ENGINE_STATUS_FAILED) {
        PositionSpec *spec = malloc(sizeof(PositionSpec));
        if (!spec) abort();
        position_spec_from_node(app->tree, target, string_sv(&app->config.rules), spec);
        engine_set_position(app->main.engine, spec);
        app->main.synced_position_id = spec->id;
        app->main.synced_node = target;
        free(spec);
    }
    if (app->main.synced_node != app->tree->current) app->analysis_valid = false;
    background_analysis_update(app);
    human_update(app);

    // Two consecutive passes end the game: score it once and stop engine play.
    const GameNode *cur = app->tree->current;
    bool two_passes = cur->has_move && point_is_pass(cur->move.point) && cur->parent && cur->parent->has_move &&
                      point_is_pass(cur->parent->move.point);
    if (two_passes && app->auto_scored_node != cur && app->main.synced_node == cur && app->main.status == ENGINE_STATUS_READY) {
        app->auto_scored_node = cur;
        app->ai_plays[STONE_BLACK] = app->ai_plays[STONE_WHITE] = false;
        app->match_mode = false;
        app->score_requested = true;
        engine_request_score(app->main.engine, app->main.synced_position_id);
    }

    if (app->score_pending && app->main.synced_node == cur && app->main.status == ENGINE_STATUS_READY) {
        app->score_pending = false;
        app->score_requested = true;
        engine_request_score(app->main.engine, app->main.synced_position_id);
    }

    // Engine's turn? In a match the second engine plays White.
    Stone to_play = app->tree->current->to_play;
    // Playing against an opponent engine (ai.engine): it answers; the main engine keeps analysing.
    EngineSlot *slot = app->match_mode && to_play == STONE_WHITE ? &app->second
                       : !app->match_mode && opponent_profile(app)  ? &app->opponent
                                                                    : &app->main;
    bool engine_turn = (app->ai_plays[to_play] || app->ai_move_once || app->match_mode) && !two_passes;
    bool anyone_waiting = app->main.waiting_move || app->second.waiting_move || app->opponent.waiting_move;
    // A playing style that picks among KataGo's candidates: analyse for the move time, then choose.
    AiStyle style = ai_style_from(string_sv(&app->config.ai_style));
    bool style_turn = engine_turn && slot == &app->main && app->ai_plays[to_play] && ai_style_picks(style) && slot->engine &&
                      slot->status == ENGINE_STATUS_READY && !anyone_waiting && app->style_gave_up != app->tree->current;
    if (app->style_node && app->style_node != app->tree->current) app->style_node = NULL;   // the position moved on
    if (style_turn) {
        if (!app->style_node) {
            app->style_node = app->tree->current;
            // Styles choose among analysed candidates: no clock set means 2 s of analysis.
            f32 think = app->config.ai_seconds_per_move > 0 ? app->config.ai_seconds_per_move : 2.0f;
            app->style_deadline = os_time_seconds() + MAX(think, 0.2f);
            if (!app->analysis_enabled) app_set_analysis(app, true);
            return;   // analysing until the deadline
        } else if (os_time_seconds() >= app->style_deadline) {
            bool fresh = app->analysis_valid && slot->synced_node == app->tree->current && app->analysis->to_play == to_play;
            i32 pick = fresh ? ai_style_pick(style, app->analysis, &app->tree->current->board) : -1;
            if (pick >= 0) {
                app->style_node = NULL;
                char coord[8];
                point_to_gtp(app->analysis->candidates[pick].move, app->analysis->width, app->analysis->height, coord);
                log_info("style %s: %s (candidate %d of %d, %.1f points below the best)", string_cstr(&app->config.ai_style), coord, pick + 1,
                         app->analysis->candidate_count, app->analysis->candidates[0].score_lead - app->analysis->candidates[pick].score_lead);
                if (app_play(app, app->analysis->candidates[pick].move) != MOVE_OK) app_set_status(app, "Style move rejected");
                return;
            }
            if (os_time_seconds() < app->style_deadline + 3.0 * MAX(app->config.ai_seconds_per_move, 1.0f)) return;   // no analysis yet
            app->style_node = NULL;   // none came: KataGo's own move below
            app->style_gave_up = app->tree->current;
        } else {
            return;
        }
    }
    if (engine_turn && slot->engine && !anyone_waiting && slot->status == ENGINE_STATUS_READY &&
        slot->synced_node == app->tree->current) {
        MoveRequest req = { slot->synced_position_id, to_play, app->config.ai_seconds_per_move,
                            style == AI_STYLE_HUMAN && slot == &app->main, app->config.ai_max_visits };
        if (req.human_style) snprintf(req.human_profile, sizeof(req.human_profile), SV_FMT, SV_ARG(app_ai_human_profile(app)));
        engine_request_move(slot->engine, &req);
        slot->waiting_move = true;
        app->ai_move_once = false;
    }
}

// ---- Game -------------------------------------------------------------------------------

void app_reload_language(App *app) {
    i18n_free(&app->i18n);
    String en = path_join(string_sv(&app->assets_dir), SV("l10n/en.properties"));
    i18n_load(&app->i18n, string_sv(&en));
    string_free(&en);
    if (!sv_eq(string_sv(&app->config.language), SV("en"))) {
        String rel = string_format("l10n/%s.properties", string_cstr(&app->config.language));
        String lang = path_join(string_sv(&app->assets_dir), string_sv(&rel));
        if (!i18n_load(&app->i18n, string_sv(&lang))) log_warn("missing translation %s", string_cstr(&lang));
        string_free(&lang);
        string_free(&rel);
    }
    app->language_version++;
}

// Settings that need KataGo restarted are collected and applied together
// (app_apply_engine_settings): restarting on every click froze the UI.
void app_apply_engine_settings(App *app) {
    if (!app->restart_main_pending && !app->restart_second_pending) return;
    bool main = app->restart_main_pending, second = app->restart_second_pending;
    app->restart_main_pending = app->restart_second_pending = false;
    if (!katago_overrides_valid(string_sv(&app->config.katago_overrides))) {
        app_set_status(app, "KataGo overrides ignored: expected key=value, key=value");
    }
    if (main) app_restart_engine(app);
    if (second && app->second.engine) app_second_engine(app, true);
    app_set_status(app, "Engine settings applied");
}

bool app_engine_settings_pending(const App *app) { return app->restart_main_pending || app->restart_second_pending; }

// The Distribution buckets (and the mistake / blunder counts) from the settings.
void app_apply_loss_limits(App *app) {
    LossLimits l;
    loss_limits_parse(string_sv(&app->config.report_winrate_limits), string_sv(&app->config.report_score_limits), &l);
    loss_limits_set(&l);
    match_rule_set((MatchRule){ app->config.report_match_moves, app->config.report_match_percent });
}

void app_config_changed(App *app, StringView key) {
    if (sv_eq(key, SV("report.winrate_limits")) || sv_eq(key, SV("report.score_limits")) || sv_starts_with(key, SV("report.match_"))) {
        app_apply_loss_limits(app);
    } else if (sv_eq(key, SV("language"))) {
        app_reload_language(app);
    } else if (sv_eq(key, SV("engine"))) {
        app->restart_main_pending = true;
    } else if (sv_eq(key, SV("rules"))) {
        app->main.synced_node = NULL;   // resync sends kata-set-rules
    } else if (sv_eq(key, SV("ai.human_profile")) || sv_eq(key, SV("ai.human_white_rank"))) {   // the opponent engine's strength
        if (app->opponent.engine) engine_set_human_profile(app->opponent.engine, app_ai_human_profile(app));
    } else if (sv_eq(key, SV("analysis.human_profile"))) {   // switched in place (kata-set-param)
        if (!app->rank_job) {   // a running rank job restores the new value when it ends
            if (app->main.engine) engine_set_human_profile(app->main.engine, app_human_profile(app));
            if (app->second.engine) engine_set_human_profile(app->second.engine, app_human_profile(app));
        }
    } else if (sv_eq(key, SV("katago.network")) || sv_eq(key, SV("katago.human_network"))) {
        app_reload_engines(app);   // the profiles resolve their network folder again
    } else if (sv_starts_with(key, SV("katago."))) {   // launch overrides: restart what is running (on Apply)
        if (app->main.engine) app->restart_main_pending = true;
        if (app->second.engine) app->restart_second_pending = true;
    } else if (sv_eq(key, SV("ai.engine"))) {
        slot_clear(&app->opponent);   // started again when it is next needed
        string_free(&app->opponent.error);
    } else if (sv_eq(key, SV("engine2"))) {
        if (app->second.engine) app->restart_second_pending = true;
    } else if (sv_starts_with(key, SV("analysis."))) {
        if (app->analysis_enabled) app_set_analysis(app, true);
        if (app->second.engine) {
            AnalysisRequest req = app_analysis_request(app);
            engine_start_analysis(app->second.engine, &req);
        }
    }
}

static void replace_tree(App *app, GameTree *tree) {
    app_batch_stop(app);
    app->region = (BoardRegion){ 0 };   // board size may change
    // The UI may still draw the old game this frame (the graph and the variation tree
    // collect node pointers while laying out and draw them afterwards, and a click on
    // New game / Open lands in between): free it at the next update instead.
    game_tree_free(app->retired_tree);
    app->retired_tree = app->tree;
    app->tree = tree;
    app->tree_generation++;
    app->human_policy_node = NULL;   // points into the freed tree (its memory may be reused)
    string_free(&app->autosave_path);   // a new game gets its own auto-save file
    app->autosaved_revision = tree->revision;
    app->autosave_last = os_time_seconds();
    app_rank_stop(app);
    app->main.synced_node = NULL;
    app->analysis_valid = false;
    app->dirty = false;
    app_edit_history_clear(app);   // the history points into the old game
}

void app_start_game(App *app, const NewGameSpec *spec) {
    replace_tree(app, game_tree_new_game(spec));
    string_free(&app->sgf_path);
    app->main.waiting_move = false;
}

bool app_open_sgf(App *app, StringView path) {
    SgfError err;
    GameTree *tree = sgf_load_file(path, &err);
    if (!tree) {
        app_set_status(app, "Cannot open " SV_FMT ": %s (line %d)", SV_ARG(path_basename(path)), err.message, err.line);
        return false;
    }
    replace_tree(app, tree);
    string_free(&app->sgf_path);
    app->sgf_path = string_from(path);
    string_free(&app->config.last_directory);
    app->config.last_directory = string_from(path_dirname(path));
    if (tree->komi_fixed) {
        app_set_status(app, "Opened " SV_FMT " (Fox komi read as %g)", SV_ARG(path_basename(path)), game_tree_komi(tree));
    } else {
        app_set_status(app, "Opened " SV_FMT, SV_ARG(path_basename(path)));
    }
    return true;
}

// Writes the game and its analysis (CGE[], plus LZ / KT if enabled) to `path`.
static bool write_game(App *app, StringView path) {
    game_tree_store_evals(app->tree);   // persist analysis as CGE[] properties
    game_tree_store_interop(app->tree, app->config.sgf_save_lz, app->config.sgf_save_kt);   // LizzieYzy / KaTrain
    bool ok = sgf_save_file(app->tree, path);
    if (ok) {
        app->autosaved_revision = app->tree->revision;   // nothing left for the auto-save
        app->autosave_last = os_time_seconds();
    }
    return ok;
}

// sgf.autosave: edits are saved a few seconds after they happen, analysis at
// most every sgf.autosave_seconds (it changes all the time while KataGo runs).
static void autosave_now(App *app) {
    if (app->sgf_path.len == 0 && app->autosave_path.len == 0) {
        char name[64];
        time_t now = time(NULL);
        strftime(name, sizeof(name), "game-%Y%m%d-%H%M%S.sgf", localtime(&now));
        String dir = path_join(string_sv(&app->config_dir), SV("autosave"));
        os_make_dirs(string_sv(&dir));
        app->autosave_path = path_join(string_sv(&dir), sv(name));
        string_free(&dir);
    }
    StringView path = app->sgf_path.len ? string_sv(&app->sgf_path) : string_sv(&app->autosave_path);
    if (write_game(app, path)) {
        if (app->sgf_path.len) app->dirty = false;   // the game's own file is up to date
    } else {
        log_warn("auto-save to " SV_FMT " failed", SV_ARG(path));
    }
}

static void autosave_update(App *app) {
    if (!app->config.sgf_autosave || app->tree->revision == app->autosaved_revision) return;
    if (!app->tree->root->first_child && !app->dirty) return;   // an empty board is not worth a file
    f64 wait = app->dirty ? 3.0 : (f64)app->config.sgf_autosave_seconds;
    if (os_time_seconds() - app->autosave_last >= wait) autosave_now(app);
}

bool app_save_sgf(App *app, StringView path) {
    if (!write_game(app, path)) {
        app_set_status(app, "Cannot save " SV_FMT, SV_ARG(path));
        return false;
    }
    if (!sv_eq(path, string_sv(&app->sgf_path))) {
        String copy = string_from(path);
        string_free(&app->sgf_path);
        app->sgf_path = copy;
    }
    app->dirty = false;
    // Analysis (live, Analyze game, rank estimates) lives in memory for the session
    // and is written only here: say how much went into the file.
    i32 analysed = 0;
    for (GameNode *n = app->tree->root; n;) {
        analysed += n->eval.valid;
        if (n->first_child) {
            n = n->first_child;
            continue;
        }
        while (n && !n->next_sibling) n = n->parent;
        if (n) n = n->next_sibling;
    }
    if (analysed) app_set_status(app, "Saved " SV_FMT " (with the analysis of %d positions)", SV_ARG(path_basename(path)), analysed);
    else app_set_status(app, "Saved " SV_FMT, SV_ARG(path_basename(path)));
    return true;
}

// Edits (moves, deletions, comments, game info, frames, results) are what make a
// game "unsaved". Analysis never does: it is kept in memory for the session and
// written with the game on Save (Ctrl+S).
void app_mark_edited(App *app) { app->dirty = true; }

MoveError app_play(App *app, Point point) {
    GameNode *before = app->tree->current;
    i32 children_before = game_node_child_count(before);
    MoveError err = game_tree_play(app->tree, (Move){ before->to_play, point });
    if (err == MOVE_OK && game_node_child_count(before) > children_before) app_mark_edited(app);
    return err;
}

void app_goto(App *app, GameNode *node) { game_tree_goto(app->tree, node); }

// ---- Game info (root SGF properties) ------------------------------------------------------

static const struct {
    const char *ident;   // SGF property
    usize offset;        // String in Config
} GAME_INFO_TEXT[] = {
    { "PB", offsetof(Config, game_info_black) },      { "PW", offsetof(Config, game_info_white) },
    { "BR", offsetof(Config, game_info_black_rank) }, { "WR", offsetof(Config, game_info_white_rank) },
    { "RE", offsetof(Config, game_info_result) },     { "EV", offsetof(Config, game_info_event) },
    { "DT", offsetof(Config, game_info_date) },
};

void app_game_info_load(App *app) {
    for (isize i = 0; i < countof(GAME_INFO_TEXT); ++i) {
        String *field = (String *)((char *)&app->config + GAME_INFO_TEXT[i].offset);
        string_free(field);
        *field = string_from(game_tree_info(app->tree, sv(GAME_INFO_TEXT[i].ident)));
    }
    app->config.game_info_komi = (f32)game_tree_komi(app->tree);
}

// KM[] on the root. Evaluations were made with the old komi; the engines resync
// with the new one.
static void set_komi(App *app, f64 komi) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%g", komi);
    game_node_set_prop(app->tree, app->tree->root, SV("KM"), sv(buffer));
    app->main.synced_node = app->second.synced_node = NULL;
    app->analysis_valid = false;
}

void app_set_komi(App *app, f64 komi) {
    komi = CLAMP(komi, -150.0, 150.0);
    if (fabs(komi - game_tree_komi(app->tree)) < 1e-6) return;
    set_komi(app, komi);
    game_tree_rebuild(app->tree, app->tree->root);
    app->config.game_info_komi = (f32)komi;
    app_mark_edited(app);
    app_set_status(app, "Komi %g", komi);
}

void app_game_info_apply(App *app) {
    GameTree *tree = app->tree;
    bool changed = false;
    for (isize i = 0; i < countof(GAME_INFO_TEXT); ++i) {
        const String *field = (const String *)((const char *)&app->config + GAME_INFO_TEXT[i].offset);
        StringView ident = sv(GAME_INFO_TEXT[i].ident);
        StringView value = sv_trim(string_sv(field));
        if (sv_eq(value, game_node_prop(tree->root, ident))) continue;
        if (value.len) game_node_set_prop(tree, tree->root, ident, value);
        else game_node_remove_prop(tree, tree->root, ident);
        changed = true;
    }
    f64 komi = sgf_normalize_komi(app->config.game_info_komi);
    if (fabs(komi - game_tree_komi(tree)) > 1e-6) {
        set_komi(app, komi);
        changed = true;
    }
    if (!changed) return;
    game_tree_rebuild(tree, tree->root);
    app_mark_edited(app);
    app_set_status(app, "Game info updated");
}

bool app_tsumego_frame(App *app) {
    GameTree *tree = app->tree;
    GameNode *cur = tree->current;
    StringView attacker = string_sv(&app->config.tsumego_attacker);
    TsumegoFrameOptions options = {
        .attacker = sv_eq(attacker, SV("black")) ? STONE_BLACK : sv_eq(attacker, SV("white")) ? STONE_WHITE : STONE_EMPTY,
        .margin = app->config.tsumego_margin,
    };
    StringView threats = string_sv(&app->config.tsumego_ko_threats);
    options.ko_threat_attacker = sv_eq(threats, SV("attacker")) || sv_eq(threats, SV("both"));
    options.ko_threat_defender = sv_eq(threats, SV("defender")) || sv_eq(threats, SV("both"));
    if (app->region.active) {   // the selected rectangle already leaves some room
        options.has_problem = true;
        options.margin = MAX(app->config.tsumego_margin - 1, 0);
        options.problem = (BoardRect){ app->region.x0, app->region.y0, app->region.x1, app->region.y1 };
    }
    TsumegoFrame *frame = malloc(sizeof(TsumegoFrame));
    if (!frame) abort();
    const char *error = NULL;
    if (!tsumego_frame_build(&cur->board, &options, frame, &error)) {
        app_set_status(app, "Tsumego frame: %s", error);
        free(frame);
        return false;
    }
    // A setup node (AB/AW/AE) after the current one, same player to move.
    GameNode *node = game_tree_append_child(tree, cur);
    char coord[3];
    for (i32 i = 0; i < frame->count; ++i) {
        const Move *m = &frame->changes[i];
        point_to_sgf(m->point, coord);
        StringView ident = m->color == STONE_BLACK ? SV("AB") : m->color == STONE_WHITE ? SV("AW") : SV("AE");
        game_node_add_prop_value(tree, node, ident, sv(coord));
    }
    game_node_set_prop(tree, node, SV("PL"), cur->to_play == STONE_WHITE ? SV("W") : SV("B"));
    game_node_set_prop(tree, node, SV("C"), SV("Tsumego frame"));
    game_tree_rebuild(tree, node);
    game_tree_goto(tree, node);
    app_mark_edited(app);
    BoardRect a = frame->area;
    app_set_region(app, (BoardRegion){ true, a.x0, a.y0, a.x1, a.y1 });
    // Ko threats: say what was placed (and when there was no room for one).
    char threats_text[160] = "";
    const bool asked[3] = { false, frame->attacker == STONE_BLACK ? options.ko_threat_attacker : options.ko_threat_defender,
                            frame->attacker == STONE_WHITE ? options.ko_threat_attacker : options.ko_threat_defender };
    for (Stone s = STONE_BLACK; s <= STONE_WHITE; ++s) {
        if (!asked[s]) continue;
        const TsumegoKoThreat *t = &frame->ko_threat[s];
        isize len = (isize)strlen(threats_text);
        char move[8];
        point_to_gtp(t->move, game_tree_width(tree), game_tree_height(tree), move);
        if (t->placed) snprintf(threats_text + len, sizeof(threats_text) - (usize)len, "; ko threat for %s at %s (%d stones)", s == STONE_BLACK ? "Black" : "White", move, t->stones);
        else snprintf(threats_text + len, sizeof(threats_text) - (usize)len, "; no room for a %s ko threat", s == STONE_BLACK ? "Black" : "White");
    }
    app_set_status(app, "Tsumego frame added (%s attacks)%s; analysis limited to the problem", frame->attacker == STONE_BLACK ? "Black" : "White",
                   threats_text);
    free(frame);
    return true;
}

// ---- Commands ---------------------------------------------------------------------------

#define APP(ctx) ((App *)(ctx))
#define CAPP(ctx) ((const App *)(ctx))

// Starts a game from the new_game.* settings (the New Game dialog edits them).
static void cmd_new(void *c) {
    App *app = APP(c);
    StringView opponent = string_sv(&app->config.new_game_opponent);
    Stone engine_color = sv_eq(opponent, SV("black")) ? STONE_BLACK : sv_eq(opponent, SV("white")) ? STONE_WHITE : STONE_EMPTY;
    // The engine's name goes into PB/PW when that player has none.
    char engine_name[64] = "";
    if (engine_color != STONE_EMPTY) {
        StringView profile = sv_starts_with(app_ai_human_profile(app), SV("rank_")) ? sv_drop(app_ai_human_profile(app), 5) : app_ai_human_profile(app);
        AiStyle style = ai_style_from(string_sv(&app->config.ai_style));
        if (style == AI_STYLE_HUMAN) snprintf(engine_name, sizeof(engine_name), "KataGo (human " SV_FMT ")", SV_ARG(profile));
        else if (style != AI_STYLE_STRONGEST) snprintf(engine_name, sizeof(engine_name), "KataGo (%s)", string_cstr(&app->config.ai_style));
        else snprintf(engine_name, sizeof(engine_name), "KataGo");
    }
    StringView black = string_sv(&app->config.new_game_black), white = string_sv(&app->config.new_game_white);
    if (engine_color == STONE_BLACK && black.len == 0) black = sv(engine_name);
    if (engine_color == STONE_WHITE && white.len == 0) white = sv(engine_name);
    NewGameSpec spec = {
        .size = app->config.new_game_size,
        .komi = app->config.new_game_komi,
        .handicap = app->config.new_game_handicap,
        .black = black,
        .white = white,
        .rules = string_sv(&app->config.rules),
    };
    app_start_game(app, &spec);
    app->ai_plays[STONE_BLACK] = engine_color == STONE_BLACK;
    app->ai_plays[STONE_WHITE] = engine_color == STONE_WHITE;
    app->play_layout = true;   // a new game: the play layout
    if (engine_color != STONE_EMPTY && ai_style_from(string_sv(&app->config.ai_style)) == AI_STYLE_HUMAN && !app->human_available) {
        app_set_status(app, "Human-style play needs the human SL network (Engine -> Download engines...); the engine plays normally");
    }
}

static void cmd_open(void *c) {
    App *app = APP(c);
    String path = { 0 };
    if (file_dialog_open_sgf(string_sv(&app->config.last_directory), &path)) app_open_sgf(app, string_sv(&path));
    else if (!file_dialog_available()) app_set_status(app, "No file dialog available: drop an SGF file onto the window");
    string_free(&path);
}

static String default_save_path(const App *app) {
    char name[64];
    time_t now = time(NULL);
    strftime(name, sizeof(name), "game-%Y%m%d-%H%M%S.sgf", localtime(&now));
    StringView dir = app->config.last_directory.len ? string_sv(&app->config.last_directory) : string_sv(&app->config_dir);
    return path_join(dir, sv(name));
}

static void cmd_save_as(void *c) {
    App *app = APP(c);
    String suggested = app->sgf_path.len ? string_from(string_sv(&app->sgf_path)) : default_save_path(app);
    String path = { 0 };
    if (file_dialog_save_sgf(string_sv(&suggested), &path)) {
        app_save_sgf(app, string_sv(&path));
    } else if (!file_dialog_available()) {
        os_make_dirs(path_dirname(string_sv(&suggested)));
        app_save_sgf(app, string_sv(&suggested));
    }
    string_free(&path);
    string_free(&suggested);
}

static void cmd_save(void *c) {
    App *app = APP(c);
    if (app->sgf_path.len) app_save_sgf(app, string_sv(&app->sgf_path));
    else cmd_save_as(c);
}

static void cmd_first(void *c) { game_tree_to_start(APP(c)->tree); }
static void cmd_last(void *c) { game_tree_to_end(APP(c)->tree); }
static void cmd_back(void *c) { game_tree_back(APP(c)->tree); }
static void cmd_forward(void *c) { game_tree_forward(APP(c)->tree); }
static void cmd_back10(void *c) {
    for (i32 i = 0; i < 10; ++i) game_tree_back(APP(c)->tree);
}
static void cmd_forward10(void *c) {
    for (i32 i = 0; i < 10; ++i) game_tree_forward(APP(c)->tree);
}
static void cmd_prev_var(void *c) { game_tree_prev_variation(APP(c)->tree); }
static void cmd_next_var(void *c) { game_tree_next_variation(APP(c)->tree); }
static bool can_go_back(const void *c) { return CAPP(c)->tree->current->parent != NULL; }
static bool can_go_forward(const void *c) { return CAPP(c)->tree->current->first_child != NULL; }
static bool has_prev_var(const void *c) {
    const GameNode *n = CAPP(c)->tree->current;
    return n->parent && n->parent->first_child != n;
}
static bool has_next_var(const void *c) { return CAPP(c)->tree->current->next_sibling != NULL; }

static void cmd_pass(void *c) { app_play(APP(c), POINT_PASS); }
static void cmd_tsumego_frame(void *c) { app_tsumego_frame(APP(c)); }
static bool has_stones(const void *c) {
    const Board *b = &CAPP(c)->tree->current->board;
    return board_stone_count(b, STONE_BLACK) + board_stone_count(b, STONE_WHITE) > 0;
}
static void cmd_delete(void *c) {
    App *app = APP(c);
    app_delete_branch(app, app->tree->current);   // undoable (Ctrl+Z)
}

// Space / Analyze. In the play layout it means "analyse now": the AI stops playing,
// the analysis layout comes up and analysis starts. Otherwise it switches analysis
// on and off.
static void cmd_toggle_analysis(void *c) {
    App *app = APP(c);
    if (app->play_layout) {
        app->play_layout = false;
        app->ai_plays[STONE_BLACK] = app->ai_plays[STONE_WHITE] = false;
        app->ai_move_once = false;
        if (!app->analysis_enabled) app_set_analysis(app, true);
        return;
    }
    app_set_analysis(app, !app->analysis_enabled);
}
static bool is_analyzing(const void *c) { return CAPP(c)->analysis_enabled; }
static bool has_engine(const void *c) { return CAPP(c)->main.engine != NULL; }

static void cmd_play_best(void *c) {
    App *app = APP(c);
    const Candidate *best = app->analysis_valid ? analysis_best(app->analysis) : NULL;
    if (best) app_play(app, best->move);
}
static bool has_best(const void *c) { return CAPP(c)->analysis_valid && CAPP(c)->analysis->candidate_count > 0; }

static void cmd_toggle_ownership(void *c) {
    App *app = APP(c);
    app->config.show_ownership = !app->config.show_ownership;
    if (app->analysis_enabled) app_set_analysis(app, true);   // re-issue with/without ownership
}
static bool shows_ownership(const void *c) { return CAPP(c)->config.show_ownership; }
static void cmd_rank(void *c) {
    App *app = c;
    if (app->rank_job || app->rank_batch || app->rank_pending) {
        app->rank_pending = false;
        app_rank_stop(app);
    } else {
        app_rank_start(app);
    }
}
static bool rank_running(const void *c) { return CAPP(c)->rank_job != NULL || CAPP(c)->rank_batch != NULL || CAPP(c)->rank_pending; }
static bool has_human_profile(const void *c) {
    return CAPP(c)->human_available && CAPP(c)->tree->root->first_child != NULL;
}
static void cmd_toggle_human(void *c) {
    App *app = c;
    app->config.show_human_policy = !app->config.show_human_policy;
    if (app->config.show_human_policy && !app->human_available) {
        app_set_status(app, "Human moves need the human SL network (Engine -> Download engines...)");
    }
}
static bool shows_human(const void *c) { return CAPP(c)->config.show_human_policy; }
static void cmd_toggle_candidates(void *c) { APP(c)->config.show_candidates = !APP(c)->config.show_candidates; }
static bool shows_candidates(const void *c) { return CAPP(c)->config.show_candidates; }
static void cmd_restart_engine(void *c) { app_restart_engine(APP(c)); }

static void cmd_score(void *c) { APP(c)->score_pending = true; }
static bool can_score(const void *c) {
    const App *app = CAPP(c);
    return app->main.engine && app->main.status != ENGINE_STATUS_FAILED && !app->score_requested && !app->score_pending;
}
static void cmd_write_result(void *c) {
    App *app = APP(c);
    if (!app->result.len) return;
    game_node_set_prop(app->tree, app->tree->root, SV("RE"), string_sv(&app->result));
    app_mark_edited(app);
    app_set_status(app, "Result saved to game");
}
static bool has_result(const void *c) { return CAPP(c)->result.len > 0 && CAPP(c)->result_position == CAPP(c)->main.synced_position_id; }

static void cmd_game_analysis(void *c) {
    App *app = APP(c);
    if (app_game_analysis_running(app)) app_batch_stop(app);
    else app_batch_start(app);
}
static bool batch_running(const void *c) { return app_game_analysis_running(CAPP(c)); }

static void cmd_second_engine(void *c) { app_second_engine(APP(c), APP(c)->second.engine == NULL); }
static bool second_running(const void *c) { return CAPP(c)->second.engine != NULL; }
static void cmd_match(void *c) {
    App *app = APP(c);
    if (app->match_mode) {
        app->match_mode = false;
        return;
    }
    if (!app->second.engine) app_second_engine(app, true);
    app->ai_plays[STONE_BLACK] = app->ai_plays[STONE_WHITE] = false;
    app->match_mode = app->second.engine != NULL;
}
static bool in_match(const void *c) { return CAPP(c)->match_mode; }

bool app_hints_hidden(const App *app) { return app->ai_plays[STONE_BLACK] || app->ai_plays[STONE_WHITE]; }
bool app_eval_hidden(const App *app) {
    return app->ai_plays[STONE_BLACK] || app->ai_plays[STONE_WHITE];   // playing the AI: no evaluation at all (Space analyses)
}


// An AI button switched on: the play layout, without analysis shown.
static void ai_button(App *app, Stone color) {
    app->ai_plays[color] = !app->ai_plays[color];
    if (app->ai_plays[color]) app->play_layout = true;
}
static void cmd_ai_black(void *c) { ai_button(APP(c), STONE_BLACK); }
static void cmd_ai_white(void *c) { ai_button(APP(c), STONE_WHITE); }
static bool ai_black(const void *c) { return CAPP(c)->ai_plays[STONE_BLACK]; }
static bool ai_white(const void *c) { return CAPP(c)->ai_plays[STONE_WHITE]; }
static void cmd_ai_move(void *c) { APP(c)->ai_move_once = true; }
static bool can_ai_move(const void *c) { return CAPP(c)->main.status == ENGINE_STATUS_READY && !CAPP(c)->main.waiting_move; }

static void cmd_sync_select(void *c) { app_sync_select(APP(c)); }
static void cmd_sync_toggle(void *c) { app_sync_toggle(APP(c)); }
// The toolbar button: pick the board on screen (always: the other window may have moved), or stop.
static void cmd_sync_screen(void *c) {
    App *app = APP(c);
    if (app_sync_running(app)) app_sync_stop(app);
    else app_sync_select(app);
}
static bool sync_on(const void *c) { return app_sync_running(CAPP(c)); }

static const CommandDef APP_COMMANDS[] = {
    { "game.new", "Start game", "Start", cmd_new, NULL, NULL },
    { "file.open", "Open…", "Open", cmd_open, NULL, NULL },
    { "file.save", "Save", "Save", cmd_save, NULL, NULL },
    { "file.save_as", "Save as…", "Save as", cmd_save_as, NULL, NULL },
    { "nav.first", "First move", "|<", cmd_first, can_go_back, NULL },
    { "nav.back10", "Back 10", "<<", cmd_back10, can_go_back, NULL },
    { "nav.back", "Back", "<", cmd_back, can_go_back, NULL },
    { "nav.forward", "Forward", ">", cmd_forward, can_go_forward, NULL },
    { "nav.forward10", "Forward 10", ">>", cmd_forward10, can_go_forward, NULL },
    { "nav.last", "Last move", ">|", cmd_last, can_go_forward, NULL },
    { "nav.prev_variation", "Previous variation", "^", cmd_prev_var, has_prev_var, NULL },
    { "nav.next_variation", "Next variation", "v", cmd_next_var, has_next_var, NULL },
    { "move.pass", "Pass", "Pass", cmd_pass, NULL, NULL },
    { "move.delete", "Delete node", "Del", cmd_delete, can_go_back, NULL },
    { "analysis.toggle", "Analyze", "Analyze", cmd_toggle_analysis, has_engine, is_analyzing },
    { "analysis.play_best", "Play best move", "Best", cmd_play_best, has_best, NULL },
    { "analysis.ownership", "Territory", "Territory", cmd_toggle_ownership, NULL, shows_ownership },
    { "analysis.human_policy", "Human moves", "Human", cmd_toggle_human, NULL, shows_human },
    { "analysis.candidates", "Candidates", "Candidates", cmd_toggle_candidates, NULL, shows_candidates },
    { "engine.restart", "Restart engine", "Restart", cmd_restart_engine, NULL, NULL },
    { "game.ai_black", "Engine plays Black", "@stone_black AI", cmd_ai_black, has_engine, ai_black },
    { "game.ai_white", "Engine plays White", "@stone_white AI", cmd_ai_white, has_engine, ai_white },
    { "game.ai_move", "Engine move", "Genmove", cmd_ai_move, can_ai_move, NULL },
    { "game.score", "Score game", "Score", cmd_score, can_score, NULL },
    { "engine.second", "Second engine", "Second engine", cmd_second_engine, has_engine, second_running },
    { "game.match", "Engine vs engine", "Match", cmd_match, has_engine, in_match },
    { "analysis.game", "Analyze game", "Analyze game", cmd_game_analysis, has_engine, batch_running },
    { "analysis.rank", "Estimate ranks", "Ranks", cmd_rank, has_human_profile, rank_running },
    { "analysis.tsumego_frame", "Tsumego frame", "Frame", cmd_tsumego_frame, has_stones, NULL },
    { "game.write_result", "Save result", "Save result", cmd_write_result, has_result, NULL },
    { "sync.select", "Sync board from screen…", "Sync", cmd_sync_select, NULL, NULL },
    { "sync.toggle", "Board sync", "Sync", cmd_sync_toggle, NULL, sync_on },
    { "sync.screen", "Sync board", "Sync", cmd_sync_screen, NULL, sync_on },
};

String app_katago_dir(const App *app) { return path_join(string_sv(&app->root_dir), SV("katago")); }

static void load_engine_profiles(App *app) {
    PathVars vars = { app->root_dir, app->exe_dir, string_sv(&app->config.katago_network), string_sv(&app->config.katago_human_network) };
    KvFile engines_kv = { 0 };
    load_kv_layers(app, "engines.kv", &engines_kv);
    engine_profiles_from_kv(&engines_kv, &vars, &app->engine_profiles);
    kv_free(&engines_kv);
    app->human_available = engine_profiles_find_human(&app->engine_profiles, SV("")) != NULL;
}

void app_reload_engines(App *app) {
    app_rank_stop(app);
    stop_engine(app);
    slot_clear(&app->second);
    slot_clear(&app->opponent);   // its profile is about to be freed
    batch_analyzer_destroy(app->analyzer);
    app->analyzer = NULL;
    engine_profiles_free(&app->engine_profiles);
    load_engine_profiles(app);
    start_engine(app);
}

// ---- Lifecycle ----------------------------------------------------------------------------

// The program was called CataGui: its folders (settings, layout, themes, the AppImage's
// downloads) move to the new name once. If the new folder already exists without
// settings (created by a test run, say), the old folder's items are moved into it.
static void migrate_old_dir(StringView dir) {
    StringView name = path_basename(dir);
    StringView old_name = sv_eq(name, SV("Gifu")) ? SV("CataGui") : sv_eq(name, SV("gifu")) ? SV("catagui") : SV("");
    if (!old_name.len) return;
    String old = path_join(path_dirname(dir), old_name);
    String settings = path_join(dir, SV("config.kv"));
    bool has_settings = os_file_exists(string_sv(&settings));
    string_free(&settings);
    if (!os_dir_exists(string_sv(&old)) || has_settings) {
        string_free(&old);
        return;
    }
    if (!os_dir_exists(dir)) {
        if (os_rename(string_sv(&old), dir)) log_info("moved " SV_FMT " to " SV_FMT, SV_ARG(string_sv(&old)), SV_ARG(dir));
        else log_warn("could not move " SV_FMT " to " SV_FMT, SV_ARG(string_sv(&old)), SV_ARG(dir));
        string_free(&old);
        return;
    }
    StringVec names = { 0 };
    i32 moved = 0;
    if (os_list_dir(string_sv(&old), &names)) {
        vec_foreach(String, n, &names) {
            String from = path_join(string_sv(&old), string_sv(n)), to = path_join(dir, string_sv(n));
            if (!os_file_exists(string_sv(&to)) && !os_dir_exists(string_sv(&to)) && os_rename(string_sv(&from), string_sv(&to))) moved++;
            string_free(&from);
            string_free(&to);
        }
    }
    string_vec_free(&names);
    os_remove_dir(string_sv(&old));   // only if it is now empty
    log_info("moved %d item(s) from " SV_FMT " to " SV_FMT, moved, SV_ARG(string_sv(&old)), SV_ARG(dir));
    string_free(&old);
}

bool app_init(App *app, const AppOptions *options) {
    *app = (App){ 0 };
    app->options = *options;
    app->analysis = calloc(1, sizeof(AnalysisSnapshot));
    app->human_policy = calloc(1, sizeof(HumanPolicy));
    if (!app->analysis) abort();
    app->exe_dir = os_executable_dir();
    app->root_dir = find_root_dir(string_sv(&app->exe_dir));
    // Inside an AppImage (a read-only mount) or a macOS app bundle (which must stay
    // unmodified) KataGo and its networks live in a writable data folder instead.
    const char *appimage = getenv("APPIMAGE");
    bool app_bundle = sv_find(string_sv(&app->exe_dir), SV(".app/Contents/MacOS")) >= 0;
    if ((appimage && *appimage) || app_bundle) {
        const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
        String base = app_bundle    ? path_join(sv(home ? home : "."), SV("Library/Application Support"))
                      : xdg && *xdg ? string_from(sv(xdg))
                                    : path_join(sv(home ? home : "."), SV(".local/share"));
        string_free(&app->root_dir);
        app->root_dir = path_join(string_sv(&base), SV("Gifu"));
        string_free(&base);
        if (!options->screenshot.len) migrate_old_dir(string_sv(&app->root_dir));
        String katago = path_join(string_sv(&app->root_dir), SV("katago"));
        os_make_dirs(string_sv(&katago));
        string_free(&katago);
    }
    String local_assets = path_join(string_sv(&app->exe_dir), SV("assets"));
    if (os_dir_exists(string_sv(&local_assets))) {
        app->assets_dir = local_assets;
    } else {
        string_free(&local_assets);
        app->assets_dir = path_join(string_sv(&app->root_dir), SV("assets"));
    }
    app->config_dir = options->portable ? string_from(string_sv(&app->exe_dir)) : os_config_dir();
    if (!options->portable && !options->screenshot.len) migrate_old_dir(string_sv(&app->config_dir));   // test screenshots leave user folders alone
    updater_cleanup(string_sv(&app->exe_dir), options->updated_from);   // what a previous update left behind
    log_info("root %s, assets %s, config %s", string_cstr(&app->root_dir), string_cstr(&app->assets_dir),
             string_cstr(&app->config_dir));

    // Config (user layer).
    String config_path = path_join(string_sv(&app->config_dir), SV("config.kv"));
    config_init_defaults(&app->config, string_sv(&config_path));
    string_free(&config_path);
    String err = { 0 };
    if (!config_load(&app->config, &err)) {
        log_error("config: %s", string_cstr(&err));
        string_free(&err);
    }
    // "AI plays like a human" became Settings -> AI play -> Style -> Human.
    if (app->config.ai_human_style && sv_eq(string_sv(&app->config.ai_style), SV("strongest"))) {
        string_free(&app->config.ai_style);
        app->config.ai_style = string_from(SV("human"));
    }
    app->config.ai_human_style = false;
    if (options->theme.len) {
        string_free(&app->config.theme);
        app->config.theme = string_from(options->theme);
    }
    // --set key=value overrides, parsed exactly like config.kv lines.
    for (i32 i = 0; i < options->setting_count; ++i) {
        StringView rest = options->settings[i];
        StringView key = sv_trim(sv_chop_char(&rest, '='));
        const ConfigField *f = config_field(key);
        f64 number = 0;
        bool flag = false;
        if (!f) log_warn("--set: unknown setting '" SV_FMT "'", SV_ARG(key));
        else if (f->type == CONFIG_STRING) config_set_string(&app->config, f, sv_trim(rest));
        else if (f->type == CONFIG_BOOL && kv_parse_bool(sv_trim(rest), &flag)) config_set_bool(&app->config, f, flag);
        else if (sv_parse_f64(rest, &number)) config_set_number(&app->config, f, number);
    }

    // i18n: English base, then the selected language on top.
    app_reload_language(app);

    // Data layers: shipped assets first, user config overrides.
    KvFile keymap_kv = { 0 };
    load_kv_layers(app, "keymap.kv", &keymap_kv);
    keymap_load_kv(&app->keymap, &keymap_kv, SV("keymap.kv"));
    kv_free(&keymap_kv);

    load_engine_profiles(app);


    command_registry_init(&app->commands, app);
    command_register_all(&app->commands, APP_COMMANDS, countof(APP_COMMANDS));
    app_edit_register_commands(app);

    app->tree = game_tree_new(19, 19, 7.5);
    if (options->sgf_path.len) app_open_sgf(app, options->sgf_path);
    if (options->region.len) {   // --region A1:G7
        StringView rest = options->region;
        StringView a = sv_chop_char(&rest, ':');
        Point p0, p1;
        i32 w = game_tree_width(app->tree), h = game_tree_height(app->tree);
        if (point_from_gtp(a, w, h, &p0) && point_from_gtp(rest, w, h, &p1) && !point_is_pass(p0) && !point_is_pass(p1)) {
            app->region = (BoardRegion){ true, MIN(p0.x, p1.x), MIN(p0.y, p1.y), MAX(p0.x, p1.x), MAX(p0.y, p1.y) };
        } else {
            log_warn("--region: expected e.g. A1:G7");
        }
    }
    if (options->start_move < 0) game_tree_to_end(app->tree);
    for (i32 i = 0; i < options->start_move; ++i) game_tree_forward(app->tree);

    app->analysis_enabled = app->config.analyze_on_start;
    app_apply_loss_limits(app);
    if (!options->headless) start_engine(app);
    app_updates_init(app);
    return true;
}

void app_shutdown(App *app) {
    app_sync_stop(app);
    app_updates_shutdown(app);
    profile_builder_destroy(app->profile_builder);
    app->profile_builder = NULL;
    profile_data_free(&app->profile);
    vec_free(&app->undo);
    vec_free(&app->redo);
    bool headless_run = app->options.screenshot.len > 0 || app->options.headless;
    if (app->config.sgf_autosave && !headless_run && app->tree->revision != app->autosaved_revision &&
        (app->tree->root->first_child || app->dirty)) {
        autosave_now(app);   // nothing is lost on exit
    }
    app_rank_stop(app);
    stop_engine(app);
    slot_clear(&app->second);
    string_free(&app->second.error);
    slot_clear(&app->opponent);
    string_free(&app->opponent.error);
    free(app->second.analysis);
    batch_analyzer_destroy(app->analyzer);
    app->analyzer = NULL;
    line_channel_wait_reapers(5000);   // engines are stopped in the background: let them finish
    game_tree_free(app->retired_tree);
    app->retired_tree = NULL;
    bool read_only = app->options.screenshot.len > 0 || app->options.headless;   // automated runs never touch user config
    if (!read_only && !config_save(&app->config)) log_warn("config not saved");
    game_tree_free(app->tree);
    config_free(&app->config);
    i18n_free(&app->i18n);
    keymap_free(&app->keymap);
    command_registry_free(&app->commands);
    engine_profiles_free(&app->engine_profiles);
    for (i32 i = 0; i < APP_LOG_LINES; ++i) string_free(&app->log_lines[i]);
    string_free(&app->exe_dir);
    string_free(&app->root_dir);
    string_free(&app->assets_dir);
    string_free(&app->config_dir);
    string_free(&app->sgf_path);
    string_free(&app->main.error);
    string_free(&app->status_message);
    string_free(&app->result);
    string_free(&app->autosave_path);
    free(app->analysis);
    free(app->human_policy);
}
