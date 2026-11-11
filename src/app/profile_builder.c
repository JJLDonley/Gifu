#include "app/profile_builder.h"

#include "base/codec.h"
#include "base/log.h"
#include "platform/os.h"
#include "platform/thread.h"
#include "core/sgf.h"
#include "platform/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ProfileBuilder {
    String folder, names, program, cache_dir;
    i32 visits;
    Thread thread;
    bool started;
    volatile bool cancel;
    Mutex lock;
    ProfileStatus status;   // under lock
    ProfileData data;       // the result (DONE)
    bool taken;
    Process *child;         // the running `--analyze` (under lock: cancel kills it)
};

// One matching game found in the folder.
typedef struct Found {
    String path, cached;   // the game, its analysed copy in the cache
    Stone color;
} Found;
typedef Vec(Found) FoundVec;

void profile_data_free(ProfileData *d) {
    vec_foreach(ProfileGame, g, &d->games) string_free(&g->path), string_free(&g->date), string_free(&g->opponent);
    vec_free(&d->games);
    *d = (ProfileData){ 0 };
}

static void set_status(ProfileBuilder *b, ProfilePhase phase, i32 done, i32 total, StringView current) {
    mutex_lock(&b->lock);
    b->status.phase = phase, b->status.done = done, b->status.total = total;
    snprintf(b->status.current, sizeof(b->status.current), SV_FMT, SV_ARG(current));
    mutex_unlock(&b->lock);
}

static void fail(ProfileBuilder *b, const char *message) {
    log_warn("profile: %s", message);
    mutex_lock(&b->lock);
    b->status.phase = PROFILE_FAILED;
    snprintf(b->status.error, sizeof(b->status.error), "%s", message);
    mutex_unlock(&b->lock);
}

static bool is_sgf(StringView name) {
    return name.len > 4 && (sv_ends_with(name, SV(".sgf")) || sv_ends_with(name, SV(".SGF")));
}

// The user's colour in the game at `path` (STONE_EMPTY: not their game, or unreadable).
static Stone user_color(StringView path, StringView names) {
    SgfError err;
    GameTree *t = sgf_load_file(path, &err);
    if (!t) return STONE_EMPTY;
    Stone c = profile_name_matches(game_tree_info(t, SV("PB")), names)   ? STONE_BLACK
              : profile_name_matches(game_tree_info(t, SV("PW")), names) ? STONE_WHITE
                                                                          : STONE_EMPTY;
    game_tree_free(t);
    return c;
}

static void scan(ProfileBuilder *b, StringView dir, i32 depth, FoundVec *out) {
    if (depth > 8 || b->cancel) return;
    StringVec names = { 0 };
    os_list_dir(dir, &names);
    vec_foreach(String, n, &names) {
        if (b->cancel) break;
        String path = path_join(dir, string_sv(n));
        if (os_dir_exists(string_sv(&path))) {
            scan(b, string_sv(&path), depth + 1, out);
        } else if (is_sgf(string_sv(n))) {
            set_status(b, PROFILE_SCANNING, (i32)out->len, 0, string_sv(n));
            Stone color = user_color(string_sv(&path), string_sv(&b->names));
            if (color != STONE_EMPTY) {
                char hex[65];
                sha256_hex(string_sv(&path), hex);
                String cached = string_format(SV_FMT "/games/%.20s.sgf", SV_ARG(string_sv(&b->cache_dir)), hex);
                vec_push(out, ((Found){ path, cached, color }));
                continue;   // `path` is kept
            }
        }
        string_free(&path);
    }
    string_vec_free(&names);
}

static bool fresh(const Found *f) {
    i64 cached = os_file_mtime(string_sv(&f->cached));
    return cached && cached >= os_file_mtime(string_sv(&f->path));
}

static i32 drain_stderr(void *arg) {
    Process *p = arg;
    char buffer[4096];
    while (process_read_stderr(p, buffer, (isize)sizeof(buffer)) > 0) {}
    return 0;
}

// Runs `gifu --analyze <games> --out <tmp>`; the analysed copies land in tmp under
// their own names (the games of one run have different names).
static bool run_analyze(ProfileBuilder *b, const Found *const *games, i32 count, StringView tmp) {
    StringView *args = calloc((usize)count + 8, sizeof(StringView));
    if (!args) abort();
    String set = string_format("analysis.game_visits=%d", b->visits);
    isize n = 0;
    args[n++] = SV("--analyze");
    for (i32 i = 0; i < count; ++i) args[n++] = string_sv(&games[i]->path);
    args[n++] = SV("--out"), args[n++] = tmp;
    args[n++] = SV("--set"), args[n++] = string_sv(&set);
    ProcessSpec spec = { string_sv(&b->program), args, n, SV(""), NULL, 0 };
    String err = { 0 };
    Process *p = process_spawn(&spec, &err);
    free(args);
    string_free(&set);
    if (!p) {
        log_warn("profile: cannot run %s: %s", string_cstr(&b->program), string_cstr(&err));
        string_free(&err);
        return false;
    }
    mutex_lock(&b->lock);
    b->child = p;
    mutex_unlock(&b->lock);
    process_close_stdin(p);
    Thread errors;
    bool errors_started = thread_start(&errors, drain_stderr, p);
    char buffer[4096];
    while (process_read_stdout(p, buffer, (isize)sizeof(buffer)) > 0) {}
    if (errors_started) thread_join(&errors);
    i32 code = -1;
    process_wait(p, -1, &code);
    mutex_lock(&b->lock);
    b->child = NULL;
    mutex_unlock(&b->lock);
    process_destroy(p);
    return code == 0;
}

static void analyse(ProfileBuilder *b, FoundVec *found) {
    const Found **todo = calloc((usize)found->len + 1, sizeof(Found *));
    if (!todo) abort();
    i32 count = 0;
    vec_foreach(Found, f, found) {
        if (!fresh(f)) todo[count++] = f;
    }
    String tmp = path_join(string_sv(&b->cache_dir), SV("tmp"));
    String games_dir = path_join(string_sv(&b->cache_dir), SV("games"));
    os_make_dirs(string_sv(&games_dir));
    i32 done = 0;
    bool *taken = calloc((usize)count + 1, sizeof(bool));
    if (!taken) abort();
    while (done < count && !b->cancel) {
        // A run: up to PROFILE_CHUNK games with different file names.
        const Found *run[PROFILE_CHUNK];
        i32 n = 0;
        for (i32 i = 0; i < count && n < PROFILE_CHUNK; ++i) {
            if (taken[i]) continue;
            bool clash = false;
            for (i32 k = 0; k < n; ++k) clash |= sv_eq(path_basename(string_sv(&run[k]->path)), path_basename(string_sv(&todo[i]->path)));
            if (clash) continue;
            run[n++] = todo[i];
            taken[i] = true;
        }
        set_status(b, PROFILE_ANALYSING, done, count, path_basename(string_sv(&run[0]->path)));
        os_remove_tree(string_sv(&tmp));
        os_make_dirs(string_sv(&tmp));
        if (!run_analyze(b, run, n, string_sv(&tmp)) && !b->cancel) log_warn("profile: an analysis run failed");
        for (i32 k = 0; k < n; ++k) {
            String out = path_join(string_sv(&tmp), path_basename(string_sv(&run[k]->path)));
            if (os_file_exists(string_sv(&out))) {
                os_remove(string_sv(&run[k]->cached));
                if (!os_rename(string_sv(&out), string_sv(&run[k]->cached))) log_warn("profile: cannot store %s", string_cstr(&run[k]->cached));
            }
            string_free(&out);
        }
        done += n;
    }
    os_remove_tree(string_sv(&tmp));
    string_free(&tmp);
    string_free(&games_dir);
    free(taken);
    free(todo);
}

static i32 newest_first(const void *a, const void *b) {
    const ProfileGame *x = a, *y = b;
    return strcmp(string_cstr(&y->date), string_cstr(&x->date));
}

static void add_up(ProfileBuilder *b, FoundVec *found) {
    enum { MAX_LINE = 4096 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    if (!line) abort();
    i32 i = 0;
    vec_foreach(Found, f, found) {
        if (b->cancel) break;
        set_status(b, PROFILE_ADDING, i++, (i32)found->len, path_basename(string_sv(&f->path)));
        SgfError err;
        GameTree *t = fresh(f) ? sgf_load_file(string_sv(&f->cached), &err) : NULL;
        if (!t) {
            b->data.skipped++;
            continue;
        }
        isize count = MIN(game_tree_active_line(t, line, MAX_LINE), (isize)MAX_LINE);
        GameReport report;
        game_report_compute(line, count, &report);
        ProfileResult result = profile_result(game_tree_info(t, SV("RE")), f->color);
        const PlayerReport *mine = &report.player[f->color];
        if (mine->moves == 0) {
            b->data.skipped++;
        } else {
            profile_add_game(&b->data.totals, &report, f->color, result);
            StyleReport style;
            style_compute(line, count, &style);
            profile_add_style(&b->data.totals, &style, f->color);
            ProfileGame g = {
                .path = string_from(string_sv(&f->path)),
                .date = string_from(game_tree_info(t, SV("DT"))),
                .opponent = string_from(game_tree_info(t, f->color == STONE_BLACK ? SV("PW") : SV("PB"))),
                .color = f->color,
                .result = result,
                .accuracy = mine->accuracy,
                .moves = mine->moves,
            };
            vec_push(&b->data.games, g);
        }
        game_tree_free(t);
    }
    free(line);
    if (b->data.games.len > 1) qsort(b->data.games.items, (usize)b->data.games.len, sizeof(ProfileGame), newest_first);
}

static i32 worker(void *arg) {
    ProfileBuilder *b = arg;
    FoundVec found = { 0 };
    set_status(b, PROFILE_SCANNING, 0, 0, SV(""));
    if (!os_dir_exists(string_sv(&b->folder))) {
        fail(b, "the games folder does not exist (Settings -> Profile)");
    } else {
        scan(b, string_sv(&b->folder), 0, &found);
        log_info("profile: %td games of yours in %s", found.len, string_cstr(&b->folder));
        if (!found.len && !b->cancel) {
            fail(b, "no games with your names in the folder (Settings -> Profile)");
        } else {
            analyse(b, &found);
            add_up(b, &found);
            if (b->cancel) fail(b, "cancelled");
            else set_status(b, PROFILE_DONE, (i32)b->data.games.len, (i32)found.len, SV(""));
        }
    }
    vec_foreach(Found, f, &found) string_free(&f->path), string_free(&f->cached);
    vec_free(&found);
    return 0;
}

ProfileBuilder *profile_builder_start(const ProfileBuildSpec *spec) {
    ProfileBuilder *b = calloc(1, sizeof(ProfileBuilder));
    if (!b) abort();
    b->folder = string_from(spec->folder);
    b->names = string_from(spec->names);
    b->program = string_from(spec->program);
    b->cache_dir = string_from(spec->cache_dir);
    b->visits = spec->visits > 0 ? spec->visits : 100;
    mutex_init(&b->lock);
    b->status.phase = PROFILE_SCANNING;
    b->started = thread_start(&b->thread, worker, b);
    if (!b->started) fail(b, "cannot start the profile thread");
    return b;
}

ProfileStatus profile_builder_status(ProfileBuilder *b) {
    mutex_lock(&b->lock);
    ProfileStatus s = b->status;
    mutex_unlock(&b->lock);
    return s;
}

bool profile_builder_take(ProfileBuilder *b, ProfileData *out) {
    if (b->taken || profile_builder_status(b).phase != PROFILE_DONE) return false;
    if (b->started) thread_join(&b->thread), b->started = false;
    *out = b->data;
    b->data = (ProfileData){ 0 };
    b->taken = true;
    return true;
}

void profile_builder_cancel(ProfileBuilder *b) {
    b->cancel = true;
    mutex_lock(&b->lock);
    if (b->child) process_kill(b->child);
    mutex_unlock(&b->lock);
}

void profile_builder_destroy(ProfileBuilder *b) {
    if (!b) return;
    profile_builder_cancel(b);
    if (b->started) thread_join(&b->thread);
    mutex_destroy(&b->lock);
    profile_data_free(&b->data);
    string_free(&b->folder), string_free(&b->names), string_free(&b->program), string_free(&b->cache_dir);
    free(b);
}
