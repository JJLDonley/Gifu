#include "app/updater.h"

#include "base/codec.h"
#include "base/log.h"
#include "base/os.h"
#include "base/thread.h"
#include "base/version_info.h"
#include "base/zip.h"
#include "engine/http.h"
#include "engine/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum Job { JOB_NONE, JOB_CHECK, JOB_DOWNLOAD } Job;

struct Updater {
    String install_dir, appimage, api_url;
    Thread thread;
    bool thread_started;
    Mutex lock;
    // Under lock.
    UpdaterPhase phase;
    ReleaseInfo release;   // the newer release on the channel (AVAILABLE), then the one being installed
    ReleaseVec releases;
    String installing;
    i64 done, total;
    String error;
    // Worker input.
    Job job;
    UpdateChannel channel;
    String staged;         // the unpacked package's root folder, or the new AppImage file
};

StringView updater_program_name(void) {
#ifdef _WIN32
    return SV("gifu.exe");
#else
    return SV("gifu");
#endif
}

// A packaged install: the release archives put the notices next to the program.
// (Build folders do not: a development build never replaces itself.)
static bool packaged(const Updater *u) {
    if (u->appimage.len) return true;
    String notices = path_join(string_sv(&u->install_dir), SV("THIRD_PARTY_NOTICES.md"));
    bool ok = os_file_exists(string_sv(&notices));
    string_free(&notices);
    return ok;
}

static String update_dir(StringView install_dir) { return path_join(install_dir, SV(".update")); }

static void set_phase(Updater *u, UpdaterPhase phase, StringView error) {
    mutex_lock(&u->lock);
    u->phase = phase;
    string_free(&u->error);
    u->error = string_from(error);
    mutex_unlock(&u->lock);
}

static void fail(Updater *u, String message) {
    log_warn("update: %s", string_cstr(&message));
    set_phase(u, UPDATER_FAILED, string_sv(&message));
    string_free(&message);
}

static void on_progress(void *user, i64 done, i64 total) {
    Updater *u = user;
    mutex_lock(&u->lock);
    u->done = done, u->total = total;
    mutex_unlock(&u->lock);
}

// ---- Worker -------------------------------------------------------------------------------------

static void run_check(Updater *u) {
    String body = { 0 }, err = { 0 };
    if (!http_get(string_sv(&u->api_url), &body, &err)) {
        fail(u, string_format("cannot reach the releases (%s)", string_cstr(&err)));
        string_free(&body), string_free(&err);
        return;
    }
    ReleaseVec all;
    if (!release_list(string_sv(&body), update_asset_suffix(u->appimage.len > 0), &all)) {
        string_free(&body);
        fail(u, string_from(SV("cannot read the release list")));
        return;
    }
    string_free(&body);
    const ReleaseInfo *newer = NULL;   // the newest on the channel, if newer than this one
    vec_foreach(ReleaseInfo, r, &all) {
        if (r->prerelease && u->channel != UPDATE_CHANNEL_ALPHA) continue;
        if (version_compare(string_sv(&r->version), SV(GIFU_VERSION)) > 0) newer = r;
        break;
    }
    if (newer) log_info("update: %s available (running %s)", string_cstr(&newer->version), GIFU_VERSION);
    else log_info("update: %s is the newest version", GIFU_VERSION);
    mutex_lock(&u->lock);
    release_info_free(&u->release);
    u->release = newer ? release_info_copy(newer) : (ReleaseInfo){ 0 };
    release_vec_free(&u->releases);
    u->releases = all;
    u->phase = newer ? UPDATER_AVAILABLE : UPDATER_UP_TO_DATE;
    mutex_unlock(&u->lock);
}

// The only folder inside `dir` (archives hold one top folder), else `dir` itself.
static String package_root(StringView dir) {
    StringVec names = { 0 };
    String root = string_from(dir);
    if (os_list_dir(dir, &names) && names.len == 1) {
        String child = path_join(dir, string_sv(&names.items[0]));
        if (os_dir_exists(string_sv(&child))) string_free(&root), root = child;
        else string_free(&child);
    }
    string_vec_free(&names);
    return root;
}

static bool extract_tar(StringView archive, StringView dest, String *error) {
    StringView args[] = { SV("-xJf"), archive, SV("-C"), dest };
    ProcessSpec spec = { SV("tar"), args, countof(args), SV(""), NULL, 0 };
    Process *p = process_spawn(&spec, error);
    if (!p) return false;
    process_close_stdin(p);
    char sink[4096];
    while (process_read_stdout(p, sink, (isize)sizeof(sink)) > 0) {}
    i32 code = -1;
    process_wait(p, -1, &code);
    process_destroy(p);
    if (code != 0 && error) *error = string_format("tar exited with %d", code);
    return code == 0;
}

static void run_download(Updater *u) {
    mutex_lock(&u->lock);
    ReleaseInfo r = { .asset_name = string_from(string_sv(&u->release.asset_name)), .asset_url = string_from(string_sv(&u->release.asset_url)),
                      .sums_url = string_from(string_sv(&u->release.sums_url)), .asset_size = u->release.asset_size };
    u->done = 0, u->total = r.asset_size > 0 ? r.asset_size : -1;
    mutex_unlock(&u->lock);
    String dir = update_dir(string_sv(&u->install_dir)), err = { 0 }, sums = { 0 };
    String download = { 0 }, unpack = { 0 };
    os_remove_tree(string_sv(&dir));
    if (!os_make_dirs(string_sv(&dir))) {
        fail(u, string_format("cannot write to %s", string_cstr(&u->install_dir)));
        goto done;
    }
    // An AppImage is downloaded next to itself (same file system: the swap is a rename).
    download = u->appimage.len ? string_format(SV_FMT ".update", SV_ARG(string_sv(&u->appimage)))
                               : path_join(string_sv(&dir), string_sv(&r.asset_name));
    set_phase(u, UPDATER_DOWNLOADING, SV(""));
    if (!http_get(string_sv(&r.sums_url), &sums, &err)) {
        fail(u, string_format("cannot download SHA256SUMS (%s)", string_cstr(&err)));
        goto done;
    }
    char want[65], have[65];
    if (!sums_find(string_sv(&sums), string_sv(&r.asset_name), want)) {
        fail(u, string_format("SHA256SUMS does not list %s", string_cstr(&r.asset_name)));
        goto done;
    }
    if (!http_download(string_sv(&r.asset_url), string_sv(&download), r.asset_size > 0 ? r.asset_size : -1, on_progress, NULL, u, &err)) {
        fail(u, string_format("download failed (%s)", string_cstr(&err)));
        goto done;
    }
    set_phase(u, UPDATER_VERIFYING, SV(""));
    if (!sha256_file_hex(string_sv(&download), have) || strcmp(want, have) != 0) {
        os_remove(string_sv(&download));
        fail(u, string_format("%s does not match its SHA-256 checksum", string_cstr(&r.asset_name)));
        goto done;
    }
    String staged = { 0 };
    if (u->appimage.len) {
        os_set_executable(string_sv(&download));
        staged = string_from(string_sv(&download));
    } else {
        unpack = path_join(string_sv(&dir), SV("new"));
        os_make_dirs(string_sv(&unpack));
        bool ok = sv_ends_with(string_sv(&r.asset_name), SV(".zip")) ? zip_extract(string_sv(&download), string_sv(&unpack), NULL, &err)
                                                                     : extract_tar(string_sv(&download), string_sv(&unpack), &err);
        os_remove(string_sv(&download));
        if (!ok) {
            fail(u, string_format("cannot unpack %s (%s)", string_cstr(&r.asset_name), string_cstr(&err)));
            goto done;
        }
        staged = package_root(string_sv(&unpack));
        String program = path_join(string_sv(&staged), updater_program_name());
        bool has_program = os_file_exists(string_sv(&program));
        string_free(&program);
        if (!has_program) {
            fail(u, string_format("%s has no %s", string_cstr(&r.asset_name), updater_program_name().data));
            string_free(&staged);
            goto done;
        }
    }
    mutex_lock(&u->lock);
    string_free(&u->staged);
    u->staged = staged;
    u->phase = UPDATER_READY;
    mutex_unlock(&u->lock);
    log_info("update: %s verified and unpacked", string_cstr(&r.asset_name));
done:
    string_free(&dir), string_free(&err), string_free(&sums), string_free(&download), string_free(&unpack);
    release_info_free(&r);
}

static i32 worker(void *arg) {
    Updater *u = arg;
    if (u->job == JOB_CHECK) run_check(u);
    else if (u->job == JOB_DOWNLOAD) run_download(u);
    return 0;
}

static void start(Updater *u, Job job, UpdaterPhase phase) {
    if (u->thread_started) thread_join(&u->thread), u->thread_started = false;
    u->job = job;
    set_phase(u, phase, SV(""));
    u->thread_started = thread_start(&u->thread, worker, u);
    if (!u->thread_started) set_phase(u, UPDATER_FAILED, SV("cannot start the update thread"));
}

// ---- Interface ----------------------------------------------------------------------------------

Updater *updater_create(StringView install_dir, StringView appimage, StringView api_url) {
    Updater *u = calloc(1, sizeof(Updater));
    if (!u) abort();
    u->install_dir = string_from(install_dir);
    u->appimage = string_from(appimage);
    u->api_url = string_from(api_url);
    mutex_init(&u->lock);
    return u;
}

void updater_check(Updater *u, UpdateChannel channel) {
    UpdaterPhase phase;
    mutex_lock(&u->lock);
    phase = u->phase;
    mutex_unlock(&u->lock);
    if (phase == UPDATER_CHECKING || phase == UPDATER_DOWNLOADING || phase == UPDATER_VERIFYING) return;   // busy
    u->channel = channel;
    start(u, JOB_CHECK, UPDATER_CHECKING);
}

void updater_install(Updater *u, StringView version) {
    mutex_lock(&u->lock);
    bool idle = u->phase == UPDATER_AVAILABLE || u->phase == UPDATER_UP_TO_DATE || u->phase == UPDATER_FAILED || u->phase == UPDATER_READY;
    const ReleaseInfo *pick = NULL;
    vec_foreach(ReleaseInfo, r, &u->releases) {
        if (sv_eq(string_sv(&r->version), version)) pick = r;
    }
    if (idle && pick) {
        ReleaseInfo copy = release_info_copy(pick);
        release_info_free(&u->release);
        u->release = copy;
        string_free(&u->installing);
        u->installing = string_from(version);
    }
    mutex_unlock(&u->lock);
    if (!idle || !pick) return;
    if (!packaged(u)) {
        set_phase(u, UPDATER_FAILED, SV("this is a development build: download the new version from the release page"));
        return;
    }
    start(u, JOB_DOWNLOAD, UPDATER_DOWNLOADING);
}

void updater_status(Updater *u, UpdaterStatus *out) {
    mutex_lock(&u->lock);
    *out = (UpdaterStatus){
        .phase = u->phase,
        .version = string_from(string_sv(&u->release.version)),
        .notes = string_from(string_sv(&u->release.notes)),
        .page_url = string_from(string_sv(&u->release.page_url)),
        .done = u->done,
        .total = u->total,
        .error = string_from(string_sv(&u->error)),
        .installing = string_from(string_sv(&u->installing)),
    };
    // The newer release is only reported while it is the offer (not once one is installing).
    if (u->phase != UPDATER_AVAILABLE) {
        string_free(&out->version), string_free(&out->notes), string_free(&out->page_url);
    }
    vec_foreach(ReleaseInfo, r, &u->releases) vec_push(&out->releases, release_info_copy(r));
    mutex_unlock(&u->lock);
    out->can_install = packaged(u);
}

void updater_status_free(UpdaterStatus *s) {
    string_free(&s->version);
    string_free(&s->notes);
    string_free(&s->page_url);
    string_free(&s->error);
    string_free(&s->installing);
    release_vec_free(&s->releases);
}

// One item swapped in: where the old one went (to undo).
typedef struct Moved {
    String target, saved;   // saved: the old item's place in .update/old ("" if there was none)
} Moved;

bool updater_apply(Updater *u, const StringView *args, isize arg_count, String *error) {
    mutex_lock(&u->lock);
    bool ready = u->phase == UPDATER_READY;
    String staged = string_from(string_sv(&u->staged));
    mutex_unlock(&u->lock);
    if (!ready) {
        string_free(&staged);
        if (error) *error = string_from(SV("no update is ready"));
        return false;
    }
    String program = { 0 };
    bool ok = true;
    if (u->appimage.len) {
        ok = os_rename(string_sv(&staged), string_sv(&u->appimage));   // replaces the running image's file
        if (!ok && error) *error = string_format("cannot replace %s", string_cstr(&u->appimage));
        program = string_from(string_sv(&u->appimage));
    } else {
        String old = path_join(string_sv(&u->install_dir), SV(".update/old"));
        os_remove_tree(string_sv(&old));
        os_make_dirs(string_sv(&old));
        StringVec names = { 0 };
        Vec(Moved) moved = { 0 };
        ok = os_list_dir(string_sv(&staged), &names);
        vec_foreach(String, n, &names) {
            if (!ok) break;
            if (sv_eq(string_sv(n), SV("katago"))) continue;   // the user's engines and networks stay
            Moved m = { path_join(string_sv(&u->install_dir), string_sv(n)), { 0 } };
            String incoming = path_join(string_sv(&staged), string_sv(n));
            if (os_file_exists(string_sv(&m.target)) || os_dir_exists(string_sv(&m.target))) {
                m.saved = path_join(string_sv(&old), string_sv(n));
                ok = os_rename(string_sv(&m.target), string_sv(&m.saved));
            }
            if (ok) ok = os_rename(string_sv(&incoming), string_sv(&m.target));
            if (!ok && error) *error = string_format("cannot replace %s", string_cstr(&m.target));
            string_free(&incoming);
            vec_push(&moved, m);
        }
        if (!ok) {   // put everything back, newest first
            for (isize i = moved.len - 1; i >= 0; --i) {
                Moved *m = &moved.items[i];
                if (os_file_exists(string_sv(&m->target)) || os_dir_exists(string_sv(&m->target))) {
                    String back = path_join(string_sv(&staged), path_basename(string_sv(&m->target)));
                    os_rename(string_sv(&m->target), string_sv(&back));
                    string_free(&back);
                }
                if (m->saved.len) os_rename(string_sv(&m->saved), string_sv(&m->target));
            }
        }
        vec_foreach(Moved, m, &moved) string_free(&m->target), string_free(&m->saved);
        vec_free(&moved);
        string_vec_free(&names);
        string_free(&old);
        program = path_join(string_sv(&u->install_dir), updater_program_name());
        os_set_executable(string_sv(&program));
    }
    string_free(&staged);
    // The new version cleans up after this one once it has exited (--updated-from).
    StringView *full = calloc((usize)arg_count + 2, sizeof(StringView));
    if (!full) abort();
    char pid[24];
    snprintf(pid, sizeof(pid), "%lld", (long long)os_process_id());
    for (isize i = 0; i < arg_count; ++i) full[i] = args[i];
    full[arg_count] = SV("--updated-from"), full[arg_count + 1] = sv(pid);
    bool started = ok && os_spawn_detached(string_sv(&program), full, arg_count + 2);
    free(full);
    if (ok && !started) {
        ok = false;   // installed, but it did not start: the user starts it (the files are in place)
        if (error) *error = string_format("updated, but %s did not start: start it again", string_cstr(&program));
    }
    if (ok) log_info("update: installed; starting %s", string_cstr(&program));
    string_free(&program);
    return ok;
}

void updater_destroy(Updater *u) {
    if (!u) return;
    if (u->thread_started) thread_join(&u->thread);
    mutex_destroy(&u->lock);
    release_info_free(&u->release);
    release_vec_free(&u->releases);
    string_free(&u->installing);
    string_free(&u->error);
    string_free(&u->staged);
    string_free(&u->install_dir);
    string_free(&u->appimage);
    string_free(&u->api_url);
    free(u);
}

typedef struct Cleanup {
    String dir;
    i64 pid;
} Cleanup;

static i32 cleanup_worker(void *arg) {
    Cleanup *c = arg;
    if (c->pid > 0) os_wait_process_exit(c->pid, 30000);   // Windows: its .exe is locked until it exits
    if (os_remove_tree(string_sv(&c->dir))) log_info("update: removed the previous version's files");
    else log_warn("update: could not remove all of %s (tried again next start)", string_cstr(&c->dir));
    string_free(&c->dir);
    free(c);
    return 0;
}

void updater_cleanup(StringView install_dir, i64 previous_pid) {
    String dir = update_dir(install_dir);
    if (!os_dir_exists(string_sv(&dir))) {
        string_free(&dir);
        return;
    }
    Cleanup *c = calloc(1, sizeof(Cleanup));
    if (!c) abort();
    *c = (Cleanup){ dir, previous_pid };
    Thread t;
    if (thread_start(&t, cleanup_worker, c)) thread_detach(&t);
    else cleanup_worker(c);
}
