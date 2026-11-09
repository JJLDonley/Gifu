// Headless batch analysis: catagui --analyze a.sgf b.sgf ... [--out DIR]
//
// Every game is submitted to the KataGo JSON analysis engine at once (it
// batches positions across games on the GPU); results are written back as
// CGE[] properties and each file is saved in place or into --out.
#include "app/headless.h"

#include "base/log.h"
#include "core/game_report.h"
#include "app/installer.h"
#include "engine/game_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HeadlessGame {
    StringView path;
    GameTree *tree;
    GameJob job;
    bool submitted;
} HeadlessGame;

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
    printf("Analyzing %d game(s) with %s, %d visits per position\n", o->analyze_count, string_cstr(&profile->name), visits);

    String err = { 0 };
    BatchAnalyzer *analyzer = batch_analyzer_create(profile, &err);
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
    }

    // Collect results.
    i32 received = 0;
    f64 last_print = 0;
    for (;;) {
        bool all_done = true;
        for (i32 i = 0; i < o->analyze_count; ++i) all_done &= !games[i].submitted || game_job_done(&games[i].job);
        if (all_done || !batch_analyzer_alive(analyzer)) break;
        TurnResult r;
        bool any = false;
        while (batch_analyzer_poll(analyzer, &r)) {
            any = true;
            for (i32 i = 0; i < o->analyze_count; ++i) {
                if (games[i].submitted && game_job_apply(&games[i].job, &r)) break;
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
            String out = o->output_dir.len ? path_join(o->output_dir, path_basename(g->path)) : string_from(g->path);
            if (o->output_dir.len) os_make_dirs(o->output_dir);
            if (sgf_save_file(g->tree, string_sv(&out))) {
                print_summary(g);
                printf("  -> %s\n", string_cstr(&out));
            } else {
                fprintf(stderr, "cannot write %s\n", string_cstr(&out));
                failures++;
            }
            string_free(&out);
        }
        game_job_free(&g->job);
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

int headless_rank(App *app) {
    const AppOptions *o = &app->options;
    const EngineProfile *profile = engine_profiles_find_human(&app->engine_profiles, o->engine.len ? o->engine : SV(""));
    if (!profile) {
        fprintf(stderr, "no engine profile with a human network (catagui --install human)\n");
        return 2;
    }
    i32 failures = 0;
    for (i32 i = 0; i < o->analyze_count; ++i) {
        SgfError sgf_err;
        GameTree *tree = sgf_load_file(o->analyze_files[i], &sgf_err);
        if (!tree) {
            fprintf(stderr, SV_FMT ": %s (line %d)\n", SV_ARG(o->analyze_files[i]), sgf_err.message, sgf_err.line);
            failures++;
            continue;
        }
        game_tree_to_end(tree);
        PositionSpec *spec = malloc(sizeof(PositionSpec));
        if (!spec) abort();
        position_spec_from_node(tree, tree->current, string_sv(&app->config.rules), spec);
        String err = { 0 };
        RankEstimator *job = rank_estimator_create(profile, spec, &err);
        free(spec);
        if (!job) {
            fprintf(stderr, "cannot start %s: %s\n", string_cstr(&profile->name), string_cstr(&err));
            string_free(&err);
            game_tree_free(tree);
            return 2;
        }
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
        } else {
            printf(SV_FMT " (%.1f s):\n", SV_ARG(path_basename(o->analyze_files[i])), os_time_seconds() - started);
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
        rank_estimator_destroy(job);
        game_tree_free(tree);
    }
    return failures ? 1 : 0;
}
