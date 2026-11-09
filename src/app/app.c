#include "app/app.h"

#include "app/file_dialog.h"
#include "base/log.h"

#include <stdarg.h>
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
        .include_ownership = app->config.show_ownership,
        .max_candidates = app->config.max_candidates,
    };
}

static void stop_engine(App *app) {
    app_batch_stop(app);
    engine_destroy(app->engine);
    app->engine = NULL;
    app->engine_status = ENGINE_STATUS_STOPPED;
    app->synced_node = NULL;
    app->ai_waiting = false;
    app->score_requested = false;
    app->score_pending = false;
    app->analysis_valid = false;
}

static void start_engine(App *app) {
    stop_engine(app);
    string_free(&app->engine_error);
    StringView wanted = app->options.engine.len ? app->options.engine : string_sv(&app->config.engine);
    if (sv_eq(wanted, SV("none"))) return;
    const EngineProfile *profile = wanted.len ? engine_profiles_find(&app->engine_profiles, wanted) : NULL;
    if (profile && !engine_profile_available(profile)) {
        log_warn("engine profile '" SV_FMT "' is not available", SV_ARG(wanted));
        profile = NULL;
    }
    if (!profile) {
        vec_foreach(EngineProfile, p, &app->engine_profiles) {
            if (engine_profile_available(p)) {
                profile = p;
                break;
            }
        }
    }
    app->engine_profile = profile;
    if (!profile) {
        app->engine_error = string_from(SV("no engine found (see assets/data/engines.kv)"));
        return;
    }
    String err = { 0 };
    app->engine = engine_create(profile, &err);
    if (!app->engine) {
        app->engine_status = ENGINE_STATUS_FAILED;
        app->engine_error = err;
        log_error("engine: %s", string_cstr(&err));
        return;
    }
    app->engine_status = ENGINE_STATUS_STARTING;
    if (app->analysis_enabled) {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(app->engine, &req);
    }
}

void app_restart_engine(App *app) {
    batch_analyzer_destroy(app->analyzer);   // may belong to the previous profile
    app->analyzer = NULL;
    start_engine(app);
}

void app_set_analysis(App *app, bool enabled) {
    app->analysis_enabled = enabled;
    if (!app->engine) return;
    if (enabled) {
        AnalysisRequest req = app_analysis_request(app);
        engine_start_analysis(app->engine, &req);
    } else {
        engine_stop_analysis(app->engine);
    }
}

static void store_node_eval(GameNode *node, const AnalysisSnapshot *a) {
    const Candidate *best = analysis_best(a);
    if (!best) return;
    NodeEval e = {
        .valid = true,
        .black_winrate = analysis_black_winrate(a, best->winrate),
        .black_score = analysis_black_score(a, best->score_lead),
        .visits = a->total_visits,
    };
    for (i32 i = 0; i < a->candidate_count && e.top_count < NODE_EVAL_TOP; ++i) e.top[e.top_count++] = a->candidates[i].move;
    node->eval = e;
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

static void flash_finish(App *app) {
    app->flash_running = false;
    game_job_free(&app->flash_job);
    if (app->batch_restore_analysis) app_set_analysis(app, true);   // resume interactive analysis
}

static void flash_stop(App *app) {
    if (!app->flash_running) return;
    if (app->analyzer) batch_analyzer_cancel(app->analyzer, string_sv(&app->flash_job.id));
    flash_finish(app);
}

// Whole game in one query to the JSON analyzer. Returns false if unavailable.
static bool flash_start(App *app) {
    if (!engine_profile_can_batch(app->engine_profile)) return false;
    if (app->analyzer && !batch_analyzer_alive(app->analyzer)) {
        batch_analyzer_destroy(app->analyzer);
        app->analyzer = NULL;
    }
    if (!app->analyzer) {
        String err = { 0 };
        app->analyzer = batch_analyzer_create(app->engine_profile, &err);
        if (!app->analyzer) {
            log_warn("analysis engine: %s (falling back to GTP)", string_cstr(&err));
            string_free(&err);
            return false;
        }
    }
    GameNode *end = app->tree->current;
    while (end->first_child) end = end->first_child;   // end of the active line
    char id[32];
    snprintf(id, sizeof(id), "flash%u", ++app->flash_serial);
    game_job_build(&app->flash_job, app->tree, end, sv(id), string_sv(&app->config.rules));
    AnalysisJob request = game_job_request(&app->flash_job, app->config.analysis_game_visits);
    if (!batch_analyzer_submit(app->analyzer, &request)) {
        game_job_free(&app->flash_job);
        return false;
    }
    app->flash_running = true;
    // Give the GPU to the batch: pause interactive analysis until it finishes.
    app->batch_restore_analysis = app->analysis_enabled;
    app_set_analysis(app, false);
    app_set_status(app, "Analyzing game (%d positions, %d visits each)", app->flash_job.turns, app->config.analysis_game_visits);
    return true;
}

static void flash_update(App *app) {
    if (!app->analyzer) return;
    TurnResult r;
    while (batch_analyzer_poll(app->analyzer, &r)) {
        if (app->flash_running && game_job_apply(&app->flash_job, &r) && game_job_done(&app->flash_job)) {
            if (app->flash_job.failed) app_set_status(app, "Game analysis failed: %s", string_cstr(&app->flash_job.error));
            else app_set_status(app, "Game analysis complete");
            app->dirty = true;   // evals are saved as CGE[] with the game
            flash_finish(app);
        }
        turn_result_free(&r);
    }
    if (app->flash_running && !batch_analyzer_alive(app->analyzer)) {
        app_set_status(app, "Analysis engine exited");
        flash_stop(app);
    }
}

void app_batch_stop(App *app) {
    flash_stop(app);
    if (!app->batch_running) return;
    app->batch_running = false;
    free(app->batch_nodes);
    app->batch_nodes = NULL;
    app->batch_count = app->batch_index = 0;
    app->synced_node = NULL;   // go back to following the current node
    if (!app->batch_restore_analysis) app_set_analysis(app, false);
}

void app_batch_start(App *app) {
    if (!app->engine) return;
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
    app->synced_node = NULL;
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
        app->dirty = true;   // evals are saved as CGE[] with the game
        app_batch_stop(app);
    }
}

void app_update(App *app) {
    flash_update(app);
    if (!app->engine) return;
    EngineEvent ev;
    while (engine_poll(app->engine, &ev)) {
        switch (ev.kind) {
            case ENGINE_EVENT_STATUS:
                app->engine_status = ev.status;
                if (ev.status == ENGINE_STATUS_READY) app_set_status(app, "%s ready", string_cstr(&app->engine_profile->name));
                if (ev.status == ENGINE_STATUS_FAILED) {
                    string_free(&app->engine_error);
                    app->engine_error = string_from(SV("engine exited (see engine log)"));
                }
                break;
            case ENGINE_EVENT_ANALYSIS:
                if (ev.analysis->position_id != app->synced_position_id || !app->synced_node) break;
                store_node_eval(app->synced_node, ev.analysis);
                if (app->synced_node == app->tree->current) {
                    memcpy(app->analysis, ev.analysis, sizeof(AnalysisSnapshot));
                    app->analysis_valid = true;
                }
                if (app->batch_running && app->batch_index < app->batch_count &&
                    app->synced_node == app->batch_nodes[app->batch_index] &&
                    ev.analysis->total_visits >= app->config.analysis_game_visits) {
                    batch_advance(app);
                }
                break;
            case ENGINE_EVENT_MOVE:
                app->ai_waiting = false;
                if (ev.position_id != app->synced_position_id) {
                    app->synced_node = NULL;   // stale answer: engine board must be resynced
                    break;
                }
                if (ev.resigned) {
                    app->ai_plays[ev.move.color] = false;
                    app_set_status(app, "%s resigns", ev.move.color == STONE_BLACK ? "Black" : "White");
                    break;
                }
                if (app_play(app, ev.move.point) != MOVE_OK) {
                    app_set_status(app, "Engine move rejected");
                    app->synced_node = NULL;
                }
                break;
            case ENGINE_EVENT_SCORE:
                app->score_requested = false;
                string_free(&app->result);
                app->result = string_from(ev.text);
                app->result_position = ev.position_id;
                app_set_status(app, "Result: " SV_FMT, SV_ARG(ev.text));
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
    if (app->synced_node != target && app->engine_status != ENGINE_STATUS_FAILED) {
        PositionSpec *spec = malloc(sizeof(PositionSpec));
        if (!spec) abort();
        position_spec_from_node(app->tree, target, string_sv(&app->config.rules), spec);
        engine_set_position(app->engine, spec);
        app->synced_position_id = spec->id;
        app->synced_node = target;
        free(spec);
    }
    if (app->synced_node != app->tree->current) app->analysis_valid = false;

    // Two consecutive passes end the game: score it once and stop engine play.
    const GameNode *cur = app->tree->current;
    bool two_passes = cur->has_move && point_is_pass(cur->move.point) && cur->parent && cur->parent->has_move &&
                      point_is_pass(cur->parent->move.point);
    if (two_passes && app->auto_scored_node != cur && app->synced_node == cur && app->engine_status == ENGINE_STATUS_READY) {
        app->auto_scored_node = cur;
        app->ai_plays[STONE_BLACK] = app->ai_plays[STONE_WHITE] = false;
        app->score_requested = true;
        engine_request_score(app->engine, app->synced_position_id);
    }

    if (app->score_pending && app->synced_node == cur && app->engine_status == ENGINE_STATUS_READY) {
        app->score_pending = false;
        app->score_requested = true;
        engine_request_score(app->engine, app->synced_position_id);
    }

    // Engine's turn?
    Stone to_play = app->tree->current->to_play;
    bool engine_turn = (app->ai_plays[to_play] || app->ai_move_once) && !two_passes;
    if (engine_turn && !app->ai_waiting && app->engine_status == ENGINE_STATUS_READY &&
        app->synced_node == app->tree->current) {
        MoveRequest req = { app->synced_position_id, to_play, app->config.ai_seconds_per_move };
        engine_request_move(app->engine, &req);
        app->ai_waiting = true;
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

void app_config_changed(App *app, StringView key) {
    if (sv_eq(key, SV("language"))) {
        app_reload_language(app);
    } else if (sv_eq(key, SV("engine"))) {
        app_restart_engine(app);
    } else if (sv_eq(key, SV("rules"))) {
        app->synced_node = NULL;   // resync sends kata-set-rules
    } else if (sv_starts_with(key, SV("analysis."))) {
        if (app->analysis_enabled) app_set_analysis(app, true);
    }
}

static void replace_tree(App *app, GameTree *tree) {
    app_batch_stop(app);
    game_tree_free(app->tree);
    app->tree = tree;
    app->synced_node = NULL;
    app->analysis_valid = false;
    app->dirty = false;
}

void app_start_game(App *app, const NewGameSpec *spec) {
    replace_tree(app, game_tree_new_game(spec));
    string_free(&app->sgf_path);
    app->ai_waiting = false;
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
    app_set_status(app, "Opened " SV_FMT, SV_ARG(path_basename(path)));
    return true;
}

bool app_save_sgf(App *app, StringView path) {
    game_tree_store_evals(app->tree);   // persist analysis as CGE[] properties
    if (!sgf_save_file(app->tree, path)) {
        app_set_status(app, "Cannot save " SV_FMT, SV_ARG(path));
        return false;
    }
    if (!sv_eq(path, string_sv(&app->sgf_path))) {
        String copy = string_from(path);
        string_free(&app->sgf_path);
        app->sgf_path = copy;
    }
    app->dirty = false;
    app_set_status(app, "Saved " SV_FMT, SV_ARG(path_basename(path)));
    return true;
}

MoveError app_play(App *app, Point point) {
    GameNode *before = app->tree->current;
    i32 children_before = game_node_child_count(before);
    MoveError err = game_tree_play(app->tree, (Move){ before->to_play, point });
    if (err == MOVE_OK && game_node_child_count(before) > children_before) app->dirty = true;
    return err;
}

void app_goto(App *app, GameNode *node) { game_tree_goto(app->tree, node); }

// ---- Commands ---------------------------------------------------------------------------

#define APP(ctx) ((App *)(ctx))
#define CAPP(ctx) ((const App *)(ctx))

// Starts a game from the new_game.* settings (the New Game dialog edits them).
static void cmd_new(void *c) {
    App *app = APP(c);
    NewGameSpec spec = {
        .size = app->config.new_game_size,
        .komi = app->config.new_game_komi,
        .handicap = app->config.new_game_handicap,
        .black = string_sv(&app->config.new_game_black),
        .white = string_sv(&app->config.new_game_white),
        .rules = string_sv(&app->config.rules),
    };
    app_start_game(app, &spec);
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
static void cmd_delete(void *c) {
    App *app = APP(c);
    if (game_tree_delete_node(app->tree, app->tree->current)) app->dirty = true;
}

static void cmd_toggle_analysis(void *c) { app_set_analysis(APP(c), !APP(c)->analysis_enabled); }
static bool is_analyzing(const void *c) { return CAPP(c)->analysis_enabled; }
static bool has_engine(const void *c) { return CAPP(c)->engine != NULL; }

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
static void cmd_toggle_candidates(void *c) { APP(c)->config.show_candidates = !APP(c)->config.show_candidates; }
static bool shows_candidates(const void *c) { return CAPP(c)->config.show_candidates; }
static void cmd_restart_engine(void *c) { app_restart_engine(APP(c)); }

static void cmd_score(void *c) { APP(c)->score_pending = true; }
static bool can_score(const void *c) {
    const App *app = CAPP(c);
    return app->engine && app->engine_status != ENGINE_STATUS_FAILED && !app->score_requested && !app->score_pending;
}
static void cmd_write_result(void *c) {
    App *app = APP(c);
    if (!app->result.len) return;
    game_node_set_prop(app->tree, app->tree->root, SV("RE"), string_sv(&app->result));
    app->dirty = true;
    app_set_status(app, "Result saved to game");
}
static bool has_result(const void *c) { return CAPP(c)->result.len > 0 && CAPP(c)->result_position == CAPP(c)->synced_position_id; }

static void cmd_game_analysis(void *c) {
    App *app = APP(c);
    if (app_game_analysis_running(app)) app_batch_stop(app);
    else app_batch_start(app);
}
static bool batch_running(const void *c) { return app_game_analysis_running(CAPP(c)); }

static void cmd_ai_black(void *c) { APP(c)->ai_plays[STONE_BLACK] = !APP(c)->ai_plays[STONE_BLACK]; }
static void cmd_ai_white(void *c) { APP(c)->ai_plays[STONE_WHITE] = !APP(c)->ai_plays[STONE_WHITE]; }
static bool ai_black(const void *c) { return CAPP(c)->ai_plays[STONE_BLACK]; }
static bool ai_white(const void *c) { return CAPP(c)->ai_plays[STONE_WHITE]; }
static void cmd_ai_move(void *c) { APP(c)->ai_move_once = true; }
static bool can_ai_move(const void *c) { return CAPP(c)->engine_status == ENGINE_STATUS_READY && !CAPP(c)->ai_waiting; }

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
    { "analysis.candidates", "Candidates", "Candidates", cmd_toggle_candidates, NULL, shows_candidates },
    { "engine.restart", "Restart engine", "Restart", cmd_restart_engine, NULL, NULL },
    { "game.ai_black", "Engine plays Black", "AI ●", cmd_ai_black, has_engine, ai_black },
    { "game.ai_white", "Engine plays White", "AI ○", cmd_ai_white, has_engine, ai_white },
    { "game.ai_move", "Engine move", "Genmove", cmd_ai_move, can_ai_move, NULL },
    { "game.score", "Score game", "Score", cmd_score, can_score, NULL },
    { "analysis.game", "Analyze game", "Analyze game", cmd_game_analysis, has_engine, batch_running },
    { "game.write_result", "Save result", "Save result", cmd_write_result, has_result, NULL },
};

// ---- Lifecycle ----------------------------------------------------------------------------

bool app_init(App *app, const AppOptions *options) {
    *app = (App){ 0 };
    app->options = *options;
    app->analysis = calloc(1, sizeof(AnalysisSnapshot));
    if (!app->analysis) abort();
    app->exe_dir = os_executable_dir();
    app->root_dir = find_root_dir(string_sv(&app->exe_dir));
    String local_assets = path_join(string_sv(&app->exe_dir), SV("assets"));
    if (os_dir_exists(string_sv(&local_assets))) {
        app->assets_dir = local_assets;
    } else {
        string_free(&local_assets);
        app->assets_dir = path_join(string_sv(&app->root_dir), SV("assets"));
    }
    app->config_dir = options->portable ? string_from(string_sv(&app->exe_dir)) : os_config_dir();
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
    PathVars vars = { app->root_dir, app->exe_dir };
    KvFile keymap_kv = { 0 };
    load_kv_layers(app, "keymap.kv", &keymap_kv);
    keymap_load_kv(&app->keymap, &keymap_kv, SV("keymap.kv"));
    kv_free(&keymap_kv);

    KvFile engines_kv = { 0 };
    load_kv_layers(app, "engines.kv", &engines_kv);
    engine_profiles_from_kv(&engines_kv, &vars, &app->engine_profiles);
    kv_free(&engines_kv);


    command_registry_init(&app->commands, app);
    command_register_all(&app->commands, APP_COMMANDS, countof(APP_COMMANDS));

    app->tree = game_tree_new(19, 19, 7.5);
    if (options->sgf_path.len) app_open_sgf(app, options->sgf_path);
    if (options->start_move < 0) game_tree_to_end(app->tree);
    for (i32 i = 0; i < options->start_move; ++i) game_tree_forward(app->tree);

    app->analysis_enabled = app->config.analyze_on_start;
    if (!options->headless) start_engine(app);
    return true;
}

void app_shutdown(App *app) {
    stop_engine(app);
    batch_analyzer_destroy(app->analyzer);
    app->analyzer = NULL;
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
    string_free(&app->engine_error);
    string_free(&app->status_message);
    string_free(&app->result);
    free(app->analysis);
}
