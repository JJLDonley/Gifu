// Headless batch analysis: catagui --analyze a.sgf b.sgf ... [--out DIR]
//
// Every game is submitted to the KataGo JSON analysis engine at once (it
// batches positions across games on the GPU); results are written back as
// CGE[] properties and each file is saved in place or into --out.
#include "app/headless.h"

#include "core/sgf_analysis.h"

#include "base/log.h"
#include "core/game_report.h"
#include "app/installer.h"
#include "engine/game_job.h"
#include "app/screen_capture.h"
#include "base/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HeadlessGame {
    StringView path;
    GameTree *tree;
    GameJob job;
    RankBatch *ranks;   // CGR[] from the same engine, when it has a human network
    bool submitted;
} HeadlessGame;

static void print_rank_line(const GameTree *tree, const RankProgress *p) {
    for (Stone c = STONE_BLACK; c <= STONE_WHITE; ++c) {
        const RankTally *t = &p->tally[c];
        f32 post[RANK_COUNT];
        rank_tally_posterior(t, post);
        i32 best = rank_tally_best(t);
        StringView name = game_tree_info(tree, c == STONE_BLACK ? SV("PB") : SV("PW"));
        printf("  %s %-20.*s ~%-4.*s (%.0f%%, %d moves)\n", c == STONE_BLACK ? "B" : "W", (int)name.len, name.data,
               SV_ARG(rank_label(best)), best >= 0 ? post[best] * 100.0f : 0.0f, t->moves);
    }
}

static const EngineProfile *pick_profile(const App *app) {
    StringView wanted = app->options.engine.len ? app->options.engine : string_sv(&app->config.engine);
    const EngineProfile *p = wanted.len ? engine_profiles_find(&app->engine_profiles, wanted) : NULL;
    if (p && engine_profile_available(p) && engine_profile_can_batch(p)) return p;
    vec_foreach(EngineProfile, it, &app->engine_profiles) {
        if (engine_profile_available(it) && engine_profile_can_batch(it)) return it;
    }
    return NULL;
}

static void print_summary(const HeadlessGame *g) {
    enum { MAX_LINE = 4096 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    if (!line) abort();
    isize n = MIN(game_tree_active_line(g->tree, line, MAX_LINE), (isize)MAX_LINE);
    GameReport r;
    game_report_compute(line, n, &r);
    const PlayerReport *b = &r.player[STONE_BLACK], *w = &r.player[STONE_WHITE];
    printf("  " SV_FMT ": Black best %.0f%% top3 %.0f%% loss %.1f%% blunders %d | White best %.0f%% top3 %.0f%% loss %.1f%% blunders %d\n",
           SV_ARG(path_basename(g->path)), player_accuracy(b) * 100.0f, b->moves ? 100.0f * (f32)b->top3 / (f32)b->moves : 0.0f,
           b->avg_winrate_loss * 100.0f, b->blunders, player_accuracy(w) * 100.0f,
           w->moves ? 100.0f * (f32)w->top3 / (f32)w->moves : 0.0f, w->avg_winrate_loss * 100.0f, w->blunders);
    free(line);
}

int headless_analyze(App *app) {
    const AppOptions *o = &app->options;
    const EngineProfile *profile = pick_profile(app);
    if (!profile) {
        fprintf(stderr, "no engine profile with a usable analysis_config (see assets/data/engines.kv)\n");
        return 2;
    }
    i32 visits = app->config.analysis_game_visits;
    bool ranks = profile->human_model.len > 0;
    printf("Analyzing %d game(s) with %s, %d visits per position%s\n", o->analyze_count, string_cstr(&profile->name), visits,
           ranks ? ", with rank estimates" : "");

    String err = { 0 };
    KataGoTuning tuning = app_katago_tuning(app);
    BatchAnalyzer *analyzer = batch_analyzer_create(profile, &tuning, &err);
    if (!analyzer) {
        fprintf(stderr, "cannot start analysis engine: %s\n", string_cstr(&err));
        string_free(&err);
        return 2;
    }

    HeadlessGame *games = calloc((usize)o->analyze_count, sizeof(HeadlessGame));
    if (!games) abort();
    i32 failures = 0, total_turns = 0;
    for (i32 i = 0; i < o->analyze_count; ++i) {
        HeadlessGame *g = &games[i];
        g->path = o->analyze_files[i];
        SgfError sgf_err;
        g->tree = sgf_load_file(g->path, &sgf_err);
        if (!g->tree) {
            fprintf(stderr, SV_FMT ": %s (line %d)\n", SV_ARG(g->path), sgf_err.message, sgf_err.line);
            failures++;
            continue;
        }
        game_tree_to_end(g->tree);   // main line
        char id[32];
        snprintf(id, sizeof(id), "game%d", i);
        game_job_build(&g->job, g->tree, g->tree->current, sv(id), string_sv(&app->config.rules));
        AnalysisJob request = game_job_request(&g->job, visits);
        g->submitted = batch_analyzer_submit(analyzer, &request);
        total_turns += g->job.turns;
        if (ranks && g->submitted) {
            g->ranks = rank_batch_create(g->job.spec, sv(id));
            rank_batch_submit(g->ranks, analyzer);
        }
    }

    // Collect results.
    i32 received = 0;
    f64 last_print = 0;
    for (;;) {
        bool all_done = true;
        for (i32 i = 0; i < o->analyze_count; ++i) {
            all_done &= !games[i].submitted || game_job_done(&games[i].job);
            all_done &= !games[i].ranks || rank_batch_progress(games[i].ranks)->finished;
        }
        if (all_done || !batch_analyzer_alive(analyzer)) break;
        TurnResult r;
        bool any = false;
        while (batch_analyzer_poll(analyzer, &r)) {
            any = true;
            for (i32 i = 0; i < o->analyze_count; ++i) {
                if (games[i].submitted && game_job_apply(&games[i].job, &r)) break;
                if (games[i].ranks && rank_batch_apply(games[i].ranks, &r)) break;
            }
            if (!r.ok && r.job_id.len == 0) fprintf(stderr, "analysis engine: %s\n", string_cstr(&r.error));
            turn_result_free(&r);
        }
        received = 0;
        for (i32 i = 0; i < o->analyze_count; ++i) received += games[i].job.received;
        if (os_time_seconds() - last_print > 2.0) {
            printf("  %d / %d positions\n", received, total_turns);
            fflush(stdout);
            last_print = os_time_seconds();
        }
        if (!any) os_sleep_ms(20);
    }
    batch_analyzer_destroy(analyzer);

    // Save.
    for (i32 i = 0; i < o->analyze_count; ++i) {
        HeadlessGame *g = &games[i];
        if (!g->tree) continue;
        if (!g->submitted || g->job.failed || g->job.received < g->job.turns) {
            fprintf(stderr, SV_FMT ": analysis incomplete%s%s\n", SV_ARG(g->path), g->job.error.len ? ": " : "", string_cstr(&g->job.error));
            failures++;
        } else {
            game_tree_store_evals(g->tree);
            const RankProgress *rp = g->ranks ? rank_batch_progress(g->ranks) : NULL;
            if (rp && rp->failed) fprintf(stderr, SV_FMT ": rank estimate failed: %s\n", SV_ARG(g->path), string_cstr(&rp->error));
            else if (rp) rank_progress_store(rp, g->tree, g->tree->current);
            game_tree_store_interop(g->tree, app->config.sgf_save_lz, app->config.sgf_save_kt);
            String out = o->output_dir.len ? path_join(o->output_dir, path_basename(g->path)) : string_from(g->path);
            if (o->output_dir.len) os_make_dirs(o->output_dir);
            if (sgf_save_file(g->tree, string_sv(&out))) {
                print_summary(g);
                if (rp && !rp->failed && rp->done) print_rank_line(g->tree, rp);
                printf("  -> %s\n", string_cstr(&out));
            } else {
                fprintf(stderr, "cannot write %s\n", string_cstr(&out));
                failures++;
            }
            string_free(&out);
        }
        game_job_free(&g->job);
        rank_batch_destroy(g->ranks);
        game_tree_free(g->tree);
    }
    free(games);
    printf("%s: %d game(s), %d failure(s)\n", failures ? "DONE WITH ERRORS" : "DONE", o->analyze_count, failures);
    return failures ? 1 : 0;
}

int headless_install(App *app) {
    const AppOptions *o = &app->options;
    StringView what = o->install;
    bool engine = sv_find(what, SV("engine")) >= 0, network = sv_find(what, SV("network")) >= 0, human = sv_find(what, SV("human")) >= 0;
    if (sv_eq(what, SV("all"))) engine = network = human = true;
    const KataGoBackend *backend = katago_backend_find(o->install_backend.len ? o->install_backend : SV("opencl"));
    if (!backend) {
        fprintf(stderr, "unknown backend; choose one of:");
        for (isize i = 0; i < katago_backend_count(); ++i) fprintf(stderr, " %s", katago_backend_at(i)->id);
        fprintf(stderr, "\n");
        return 2;
    }
    String dir = o->install_dir.len ? string_from(o->install_dir) : app_katago_dir(app);
    printf("Installing into %s (%s%s%s) with backend %s\n", string_cstr(&dir), engine ? "engine " : "", network ? "network " : "",
           human ? "human-network" : "", backend->id);
    Installer *in = calloc(1, sizeof(Installer));
    if (!in) abort();
    installer_start(in, string_sv(&dir), backend, engine, network, human);
    char last_label[192] = "";
    f64 last_print = 0;
    for (;;) {
        InstallState s = installer_state(in);
        if (strcmp(s.label, last_label) != 0 || os_time_seconds() - last_print > 5.0) {
            if (s.total > 0) printf("  %s  %.1f / %.1f MB\n", s.label, (f64)s.done / 1048576.0, (f64)s.total / 1048576.0);
            else printf("  %s\n", s.label);
            fflush(stdout);
            snprintf(last_label, sizeof(last_label), "%s", s.label);
            last_print = os_time_seconds();
        }
        if (s.finished) break;
        os_sleep_ms(100);
    }
    InstallState s = installer_state(in);
    installer_finish(in);
    free(in);
    InstallStatus st = installer_detect(string_sv(&dir), backend);
    printf("%s: engine=%d network=%d human=%d%s%s\n", s.failed ? "FAILED" : "DONE", st.engine, st.network, st.human,
           s.failed ? " - " : "", s.failed ? s.error : "");
    string_free(&dir);
    return s.failed ? 1 : 0;
}

// Starts the engine and waits until it answers (KataGo loads its networks first).
static Engine *start_ready_engine(const EngineProfile *profile, StringView human_profile, const KataGoTuning *tuning) {
    String err = { 0 };
    Engine *engine = engine_create(profile, human_profile, tuning, &err);
    if (!engine) {
        fprintf(stderr, "cannot start %s: %s\n", string_cstr(&profile->name), string_cstr(&err));
        string_free(&err);
        return NULL;
    }
    for (f64 until = os_time_seconds() + 300.0; os_time_seconds() < until;) {
        EngineEvent ev;
        while (engine_poll(engine, &ev)) {
            if (ev.kind == ENGINE_EVENT_STATUS && ev.status == ENGINE_STATUS_READY) return engine;
            if (ev.kind == ENGINE_EVENT_STATUS && ev.status == ENGINE_STATUS_FAILED) {
                fprintf(stderr, "%s exited during startup\n", string_cstr(&profile->name));
                engine_destroy(engine);
                return NULL;
            }
        }
        os_sleep_ms(20);
    }
    fprintf(stderr, "%s did not become ready\n", string_cstr(&profile->name));
    engine_destroy(engine);
    return NULL;
}

// Prints a game's estimate and, with --out, saves its per-move CGR[]. Returns false on a write error.
static bool rank_report(const App *app, StringView path, GameTree *tree, const RankProgress *p, f64 seconds) {
    printf(SV_FMT " (%.1f s):\n", SV_ARG(path_basename(path)), seconds);
    print_rank_line(tree, p);
    if (!app->options.output_dir.len) return true;   // with --out: save the per-move estimates as CGR[]
    rank_progress_store(p, tree, tree->current);
    String out = path_join(app->options.output_dir, path_basename(path));
    os_make_dirs(app->options.output_dir);
    bool ok = sgf_save_file(tree, string_sv(&out));
    if (ok) printf("  -> %s\n", string_cstr(&out));
    else fprintf(stderr, "cannot write %s\n", string_cstr(&out));
    string_free(&out);
    return ok;
}

static GameTree *load_main_line(StringView path) {
    SgfError sgf_err;
    GameTree *tree = sgf_load_file(path, &sgf_err);
    if (!tree) {
        fprintf(stderr, SV_FMT ": %s (line %d)\n", SV_ARG(path), sgf_err.message, sgf_err.line);
        return NULL;
    }
    game_tree_to_end(tree);
    return tree;
}

// Every game at once on the JSON analyzer: 1-visit human-policy queries, batched on the GPU.
static int rank_batched(App *app, const EngineProfile *profile) {
    const AppOptions *o = &app->options;
    String err = { 0 };
    KataGoTuning tuning = app_katago_tuning(app);
    BatchAnalyzer *analyzer = batch_analyzer_create(profile, &tuning, &err);
    if (!analyzer) {
        fprintf(stderr, "cannot start analysis engine: %s\n", string_cstr(&err));
        string_free(&err);
        return 2;
    }
    printf("Estimating ranks with %s (analysis engine)\n", string_cstr(&profile->name));
    GameTree **trees = calloc((usize)o->analyze_count, sizeof(GameTree *));
    RankBatch **batches = calloc((usize)o->analyze_count, sizeof(RankBatch *));
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    if (!trees || !batches || !spec) abort();
    i32 failures = 0;
    f64 started = os_time_seconds();
    for (i32 i = 0; i < o->analyze_count; ++i) {
        trees[i] = load_main_line(o->analyze_files[i]);
        if (!trees[i]) {
            failures++;
            continue;
        }
        position_spec_from_node(trees[i], trees[i]->current, string_sv(&app->config.rules), spec);
        char id[32];
        snprintf(id, sizeof(id), "rank%d", i);
        batches[i] = rank_batch_create(spec, sv(id));
        rank_batch_submit(batches[i], analyzer);
    }
    free(spec);
    f64 last_print = 0;
    for (;;) {
        bool all_done = true;
        i32 done = 0, total = 0;
        for (i32 i = 0; i < o->analyze_count; ++i) {
            if (!batches[i]) continue;
            const RankProgress *p = rank_batch_progress(batches[i]);
            all_done &= p->finished;
            done += p->done, total += p->total;
        }
        if (all_done || !batch_analyzer_alive(analyzer)) break;
        TurnResult r;
        bool any = false;
        while (batch_analyzer_poll(analyzer, &r)) {
            any = true;
            for (i32 i = 0; i < o->analyze_count; ++i) {
                if (batches[i] && rank_batch_apply(batches[i], &r)) break;
            }
            if (!r.ok && r.job_id.len == 0) fprintf(stderr, "analysis engine: %s\n", string_cstr(&r.error));
            turn_result_free(&r);
        }
        if (os_time_seconds() - last_print > 2.0) {
            printf("  %d / %d moves\n", done, total);
            fflush(stdout);
            last_print = os_time_seconds();
        }
        if (!any) os_sleep_ms(10);
    }
    batch_analyzer_destroy(analyzer);
    for (i32 i = 0; i < o->analyze_count; ++i) {
        if (!batches[i]) continue;
        const RankProgress *p = rank_batch_progress(batches[i]);
        if (!p->finished || p->failed) {
            fprintf(stderr, SV_FMT ": rank estimate %s%s\n", SV_ARG(o->analyze_files[i]), p->failed ? "failed: " : "incomplete",
                    string_cstr(&p->error));
            failures++;
        } else if (!rank_report(app, o->analyze_files[i], trees[i], p, os_time_seconds() - started)) {
            failures++;
        }
        rank_batch_destroy(batches[i]);
        game_tree_free(trees[i]);
    }
    free(batches);
    free(trees);
    return failures ? 1 : 0;
}

int headless_rank(App *app) {
    const AppOptions *o = &app->options;
    StringView wanted = o->engine.len ? o->engine : string_sv(&app->config.engine);
    const EngineProfile *profile = engine_profiles_find_human(&app->engine_profiles, wanted);
    if (!profile) {
        fprintf(stderr, "no engine profile with a human network (catagui --install human)\n");
        return 2;
    }
    if (engine_profile_can_batch(profile)) return rank_batched(app, profile);
    // No analysis config: replay each game over GTP.
    KataGoTuning tuning = app_katago_tuning(app);
    Engine *engine = start_ready_engine(profile, app_human_profile(app), &tuning);
    if (!engine) return 2;
    printf("Estimating ranks with %s\n", string_cstr(&profile->name));
    i32 failures = 0;
    PositionSpec *spec = malloc(sizeof(PositionSpec));
    if (!spec) abort();
    for (i32 i = 0; i < o->analyze_count; ++i) {
        GameTree *tree = load_main_line(o->analyze_files[i]);
        if (!tree) {
            failures++;
            continue;
        }
        position_spec_from_node(tree, tree->current, string_sv(&app->config.rules), spec);
        RankEstimator *job = rank_estimator_start(engine, spec, app_human_profile(app));
        f64 started = os_time_seconds(), last_print = 0;
        const RankProgress *p = rank_estimator_progress(job);
        while (!p->finished) {
            rank_estimator_update(job);
            if (os_time_seconds() - last_print > 2.0) {
                printf("  %d / %d moves\n", p->done, p->total);
                fflush(stdout);
                last_print = os_time_seconds();
            }
            os_sleep_ms(5);
        }
        if (p->failed) {
            fprintf(stderr, SV_FMT ": %s\n", SV_ARG(o->analyze_files[i]), string_cstr(&p->error));
            failures++;
        } else if (!rank_report(app, o->analyze_files[i], tree, p, os_time_seconds() - started)) {
            failures++;
        }
        rank_estimator_destroy(job);
        game_tree_free(tree);
    }
    free(spec);
    engine_destroy(engine);
    return failures ? 1 : 0;
}

int headless_capture(App *app) {
    const AppOptions *o = &app->options;
    ScreenRect area = { 0 }, *rect = NULL;
    if (o->capture_area.len) {
        StringView rest = o->capture_area;
        i64 v[4];
        for (i32 i = 0; i < 4; ++i) {
            if (!sv_parse_i64(sv_trim(sv_chop_char(&rest, ',')), &v[i])) {
                fprintf(stderr, "--capture-area: expected X,Y,W,H\n");
                return 2;
            }
        }
        area = (ScreenRect){ (i32)v[0], (i32)v[1], (i32)v[2], (i32)v[3] };
        rect = &area;
    }
    String err = { 0 };
    ScreenCapture *capture = screen_capture_open(string_sv(&app->config.sync_capture), string_sv(&app->config.sync_capture_command), &err);
    if (!capture) {
        fprintf(stderr, "screen capture: %s\n", string_cstr(&err));
        string_free(&err);
        return 2;
    }
    RgbaImage image;
    f64 started = os_time_seconds();
    bool ok = screen_capture_grab(capture, rect, &image, &err);
    f64 took = os_time_seconds() - started;
    if (ok) {
        String png = { 0 };
        png_encode(&image, &png);
        ok = os_write_file(o->capture, string_sv(&png));
        printf(SV_FMT ": %dx%d in %.0f ms -> " SV_FMT "\n", SV_ARG(screen_capture_name(capture)), image.width, image.height, took * 1000.0, SV_ARG(o->capture));
        string_free(&png);
        rgba_image_free(&image);
    } else {
        fprintf(stderr, "screen capture (" SV_FMT "): %s\n", SV_ARG(screen_capture_name(capture)), string_cstr(&err));
    }
    string_free(&err);
    screen_capture_close(capture);
    return ok ? 0 : 1;
}
