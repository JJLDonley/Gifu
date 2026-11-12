#include "app/installer.h"
#include "base/codec.h"

#include "base/json.h"
#include "base/log.h"
#include "platform/os.h"
#include "base/zip.h"
#include "engine/http.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const KataGoBackend BACKENDS[] = {
    { "opencl", "opencl", "OpenCL (any GPU: Intel, AMD, NVIDIA)" },
    { "eigenavx2", "eigenavx2", "CPU (AVX2)" },
    { "eigen", "eigen", "CPU (compatible)" },
    { "cuda12.8-cudnn9.8.0", "cuda", "NVIDIA CUDA" },
};

static const NetworkFamily FAMILIES[] = {
    { "b6c96", "6 blocks", "Tiny: very fast, weakest (5 MB)" },
    { "b10c128", "10 blocks", "Small and fast (14 MB)" },
    { "b15c192", "15 blocks", "Small (45 MB)" },
    { "b18c384nbt", "18 blocks", "Recommended: strong and fast on any GPU (93 MB)" },
    { "b28c512nbt", "28 blocks", "Strongest per visit; needs a strong GPU (258 MB)" },
    { "b40c256", "40 blocks", "Older large size, slower (165 MB)" },
    { "b60c320", "60 blocks", "Older largest size, slowest (384 MB)" },
};

isize katago_family_count(void) { return countof(FAMILIES); }
const NetworkFamily *katago_family_at(isize i) { return i >= 0 && i < countof(FAMILIES) ? &FAMILIES[i] : NULL; }
isize katago_family_find(StringView id) {
    for (isize i = 0; i < countof(FAMILIES); ++i) {
        if (sv_eq(sv(FAMILIES[i].id), id)) return i;
    }
    return -1;
}
u32 katago_default_families(void) { return 1u << katago_family_find(SV(KATAGO_NETWORK_FAMILY)); }

isize katago_backend_count(void) { return countof(BACKENDS); }
const KataGoBackend *katago_backend_at(isize i) { return i >= 0 && i < countof(BACKENDS) ? &BACKENDS[i] : NULL; }
const KataGoBackend *katago_backend_find(StringView id) {
    for (isize i = 0; i < countof(BACKENDS); ++i) {
        if (sv_eq(sv(BACKENDS[i].id), id)) return &BACKENDS[i];
    }
    return NULL;
}

void download_asset_free(DownloadAsset *a) {
    string_free(&a->name);
    string_free(&a->url);
    string_free(&a->version);
}

// ---- Pickers ----------------------------------------------------------------------------------

typedef bool (*AssetMatch)(StringView name, const void *ctx);

static bool pick_asset(StringView json, AssetMatch match, const void *ctx, DownloadAsset *out) {
    JsonDoc doc = { 0 };
    if (!json_parse(json, &doc, NULL)) return false;
    JsonValue releases = json_root(&doc);
    bool found = false;
    for (isize r = 0; r < json_array_len(releases) && !found; ++r) {
        JsonValue rel = json_array_at(releases, r);
        if (json_bool(json_get(rel, "draft"), false)) continue;
        JsonValue assets = json_get(rel, "assets");
        for (isize a = 0; a < json_array_len(assets) && !found; ++a) {
            JsonValue asset = json_array_at(assets, a);
            StringView name = json_string(json_get(asset, "name"), SV(""));
            if (!match(name, ctx)) continue;
            *out = (DownloadAsset){
                .name = string_from(name),
                .url = string_from(json_string(json_get(asset, "browser_download_url"), SV(""))),
                .version = string_from(json_string(json_get(rel, "tag_name"), SV(""))),
                .size = json_int(json_get(asset, "size"), -1),
            };
            found = out->url.len > 0;
        }
    }
    json_free(&doc);
    return found;
}

typedef struct EngineMatch {
    StringView backend, os_name;
} EngineMatch;

static bool match_engine(StringView name, const void *ctx) {
    const EngineMatch *m = ctx;
    // katago-v<ver>-<backend>-<os>-x64.zip  (not the "+bs50" variants)
    String suffix = string_format("-" SV_FMT "-" SV_FMT "-x64.zip", SV_ARG(m->backend), SV_ARG(m->os_name));
    bool ok = sv_starts_with(name, SV("katago-v")) && sv_ends_with(name, string_sv(&suffix));
    string_free(&suffix);
    return ok;
}

static bool match_human(StringView name, const void *ctx) {
    UNUSED(ctx);
    return sv_find(name, SV("humanv")) >= 0 && sv_ends_with(name, SV(".bin.gz"));
}

bool katago_pick_engine_asset(StringView releases_json, StringView backend, StringView os_name, DownloadAsset *out) {
    EngineMatch m = { backend, os_name };
    return pick_asset(releases_json, match_engine, &m, out);
}

bool katago_pick_human_asset(StringView releases_json, DownloadAsset *out) {
    return pick_asset(releases_json, match_human, NULL, out);
}

static bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

bool katago_strongest_network(StringView html, String *name) {
    isize at = sv_find(html, SV("Strongest confidently-rated network"));
    if (at < 0) return false;
    StringView rest = sv_drop(html, at);
    isize k = sv_find(rest, SV("kata1-"));
    if (k < 0 || k > 2000) return false;   // must be right after the label
    rest = sv_drop(rest, k);
    isize len = 0;
    while (len < rest.len && is_name_char(rest.data[len])) ++len;
    StringView n = sv_take(rest, len);
    if (sv_ends_with(n, SV(".bin.gz"))) n = sv_take(n, n.len - 7);
    if (n.len < 8) return false;
    *name = string_from(n);
    return true;
}

// Number right after `marker` in `text` (e.g. the Elo before " ± "), or false.
static bool number_before(StringView text, StringView marker, f64 *out) {
    isize at = sv_find(text, marker);
    if (at <= 0) return false;
    isize start = at;
    while (start > 0 && ((text.data[start - 1] >= '0' && text.data[start - 1] <= '9') || text.data[start - 1] == '.')) --start;
    return sv_parse_f64(sv_trim(sv_take(sv_drop(text, start), at - start)), out);
}

bool katago_best_network(StringView html, StringView family, i64 min_games, String *name) {
    f64 best_elo = -1;
    bool found = false;
    for (isize at = sv_find(html, SV("<tr")); at >= 0;) {
        StringView row = sv_drop(html, at + 3);
        isize end = sv_find(row, SV("</tr>"));
        if (end < 0) break;
        StringView rest = sv_drop(row, end);
        row = sv_take(row, end);
        at = sv_find(rest, SV("<tr"));
        if (at >= 0) at += html.len - rest.len;
        // <td> name ... </td> <td> date </td> <td> 14638.3 ± 26.8 - (1,610 games) </td>
        isize td = sv_find(row, SV("<td>"));
        if (td < 0) continue;
        StringView cell = sv_trim_left(sv_drop(row, td + 4));
        isize len = 0;
        while (len < cell.len && is_name_char(cell.data[len])) ++len;
        StringView n = sv_take(cell, len);
        if (!sv_starts_with(n, SV("kata1-")) || !sv_starts_with(sv_drop(n, 6), family) || n.len <= 6 + family.len ||
            n.data[6 + family.len] != '-') {
            continue;
        }
        f64 elo = 0;
        if (!number_before(row, SV(" \xC2\xB1"), &elo)) continue;   // "±" (UTF-8)
        isize paren = sv_find(row, SV(" games)"));
        if (paren < 0) continue;
        isize open = paren;
        while (open > 0 && row.data[open - 1] != '(') --open;
        i64 games = 0;
        for (isize i = open; i < paren; ++i) {
            if (row.data[i] >= '0' && row.data[i] <= '9') games = games * 10 + (row.data[i] - '0');
        }
        if (games < min_games || elo <= best_elo) continue;
        best_elo = elo;
        string_free(name);
        *name = string_from(n);
        found = true;
    }
    return found;
}

String katago_network_url_in(StringView html, StringView name) {
    // The page links each model file: ".../models/kata1/<name>.bin.gz" (older ones .txt.gz).
    String needle = string_format("models/kata1/" SV_FMT ".", SV_ARG(name));
    isize at = sv_find(html, string_sv(&needle));
    string_free(&needle);
    if (at >= 0) {
        StringView ext = sv_drop(html, at + 13 + name.len);
        if (sv_starts_with(ext, SV(".txt.gz"))) {
            return string_format("https://media.katagotraining.org/uploaded/networks/models/kata1/" SV_FMT ".txt.gz", SV_ARG(name));
        }
    }
    return katago_network_url(name);
}

String katago_network_url(StringView name) {
    return string_format("https://media.katagotraining.org/uploaded/networks/models/kata1/" SV_FMT ".bin.gz", SV_ARG(name));
}

// ---- Detection ------------------------------------------------------------------------------------

// Bit i set: a "kata1-<family i>-....bin.gz" is in `dir`.
static u32 dir_families(StringView dir) {
    StringVec names = { 0 };
    u32 bits = 0;
    if (os_list_dir(dir, &names)) {
        vec_foreach(String, n, &names) {
            for (isize i = 0; i < countof(FAMILIES); ++i) {
                StringView name = string_sv(n);
                if (!sv_starts_with(name, SV("kata1-")) || !(sv_ends_with(name, SV(".bin.gz")) || sv_ends_with(name, SV(".txt.gz")))) continue;
                StringView rest = sv_drop(name, 6);
                StringView id = sv(FAMILIES[i].id);
                if (sv_starts_with(rest, id) && rest.len > id.len && rest.data[id.len] == '-') bits |= 1u << i;
            }
        }
    }
    string_vec_free(&names);
    return bits;
}

static bool dir_has_network(StringView dir) {
    StringVec names = { 0 };
    bool found = false;
    if (os_list_dir(dir, &names)) {
        vec_foreach(String, n, &names) found |= sv_ends_with(string_sv(n), SV(".bin.gz")) || sv_ends_with(string_sv(n), SV(".txt.gz"));
    }
    string_vec_free(&names);
    return found;
}

InstallStatus installer_detect(StringView katago_dir, const KataGoBackend *backend) {
    InstallStatus s = { 0 };
    String engine_dir = string_format(SV_FMT "/%s-%s", SV_ARG(katago_dir), os_platform_name(), backend->dir);
    String exe = path_join(string_sv(&engine_dir), sv(sv_eq(sv(os_platform_name()), SV("windows")) ? "katago.exe" : "katago"));
    s.engine = os_file_exists(string_sv(&exe));
    String networks = path_join(katago_dir, SV("networks"));
    String human = path_join(string_sv(&networks), SV("human"));
    s.network = dir_has_network(string_sv(&networks));
    s.families = dir_families(string_sv(&networks));
    s.human = dir_has_network(string_sv(&human));
    string_free(&engine_dir);
    string_free(&exe);
    string_free(&networks);
    string_free(&human);
    return s;
}

// ---- Worker -----------------------------------------------------------------------------------------

static void set_label(Installer *in, const char *fmt, ...) PRINTF_LIKE(2, 3);
static void set_label(Installer *in, const char *fmt, ...) {
    mutex_lock(&in->lock);
    va_list args;
    va_start(args, fmt);
    vsnprintf(in->state.label, sizeof(in->state.label), fmt, args);
    va_end(args);
    in->state.done = 0;
    in->state.total = -1;
    mutex_unlock(&in->lock);
    log_info("installer: %s", in->state.label);
}

static void on_progress(void *user, i64 done, i64 total) {
    Installer *in = user;
    mutex_lock(&in->lock);
    in->state.done = done;
    in->state.total = total;
    mutex_unlock(&in->lock);
}

static bool on_cancel(void *user) { return ((Installer *)user)->cancel; }

static bool fail(Installer *in, String *error, const char *what) {
    mutex_lock(&in->lock);
    snprintf(in->state.error, sizeof(in->state.error), "%s: %s", what, error ? string_cstr(error) : "");
    in->state.failed = true;
    mutex_unlock(&in->lock);
    if (error) string_free(error);
    return false;
}

static bool install_engine(Installer *in, StringView releases) {
    const InstallRequest *rq = &in->request;
    DownloadAsset asset = { 0 };
    if (!katago_pick_engine_asset(releases, sv(rq->backend->id), sv(os_platform_name()), &asset)) {
        String e = string_format("no %s build for %s in recent KataGo releases", rq->backend->id, os_platform_name());
        return fail(in, &e, "KataGo");
    }
    String zip = path_join(string_sv(&rq->katago_dir), string_sv(&asset.name));
    String dir = string_format(SV_FMT "/%s-%s", SV_ARG(string_sv(&rq->katago_dir)), os_platform_name(), rq->backend->dir);
    String err = { 0 };
    bool ok = true;
    set_label(in, "Downloading KataGo %s (%s)", string_cstr(&asset.version), rq->backend->label);
    if (!http_download(string_sv(&asset.url), string_sv(&zip), asset.size, on_progress, on_cancel, in, &err)) {
        ok = fail(in, &err, "KataGo download");
    } else {
        set_label(in, "Extracting %s", string_cstr(&asset.name));
        ZipStats stats;
        if (!zip_extract(string_sv(&zip), string_sv(&dir), &stats, &err)) ok = fail(in, &err, "KataGo extract");
        else {
            String exe = path_join(string_sv(&dir), SV("katago"));
            os_set_executable(string_sv(&exe));
            string_free(&exe);
            log_info("installer: extracted %d files into %s", stats.files, string_cstr(&dir));
        }
        os_remove(string_sv(&zip));
    }
    string_free(&zip);
    string_free(&dir);
    download_asset_free(&asset);
    return ok;
}

static bool download_network(Installer *in, StringView url, StringView dest, i64 size, const char *label) {
    if (os_file_exists(dest)) return true;
    set_label(in, "Downloading %s", label);
    if (size < 0) size = http_content_length(url);
    String err = { 0 };
    if (!http_download(url, dest, size, on_progress, on_cancel, in, &err)) return fail(in, &err, label);
    return true;
}

static i32 worker_main(void *arg) {
    Installer *in = arg;
    const InstallRequest *rq = &in->request;
    String releases = { 0 }, err = { 0 };
    bool ok = true;
    if (rq->engine || rq->human) {
        set_label(in, "Looking up KataGo releases");
        if (!http_get(SV("https://api.github.com/repos/lightvector/KataGo/releases?per_page=40"), &releases, &err)) {
            ok = fail(in, &err, "GitHub releases");
        }
    }
    if (ok && rq->engine && !in->cancel) ok = install_engine(in, string_sv(&releases));
    if (ok && rq->networks && !in->cancel) {
        set_label(in, "Looking up networks on katagotraining.org");
        String html = { 0 };
        if (!http_get(SV("https://katagotraining.org/networks/"), &html, &err)) ok = fail(in, &err, "katagotraining.org");
        for (isize i = 0; ok && i < countof(FAMILIES) && !in->cancel; ++i) {
            if (!(rq->networks & (1u << i))) continue;
            String name = { 0 };
            // The best-rated of the family (fewer rating games for the rarer old sizes).
            if (!katago_best_network(string_sv(&html), sv(FAMILIES[i].id), KATAGO_NETWORK_MIN_GAMES, &name) &&
                !katago_best_network(string_sv(&html), sv(FAMILIES[i].id), 0, &name)) {
                String e = string_format("no %s network on the page", FAMILIES[i].id);
                ok = fail(in, &e, "katagotraining.org");
                break;
            }
            String url = katago_network_url_in(string_sv(&html), string_sv(&name));
            StringView ext = sv_ends_with(string_sv(&url), SV(".txt.gz")) ? SV(".txt.gz") : SV(".bin.gz");
            String file = string_format(SV_FMT "/networks/%s" SV_FMT, SV_ARG(string_sv(&rq->katago_dir)), string_cstr(&name), SV_ARG(ext));
            ok = download_network(in, string_sv(&url), string_sv(&file), -1, string_cstr(&name));
            string_free(&url);
            string_free(&file);
            string_free(&name);
        }
        string_free(&html);
    }
    if (ok && rq->human && !in->cancel) {
        DownloadAsset human = { 0 };
        if (!katago_pick_human_asset(string_sv(&releases), &human)) {
            String e = string_from(SV("no human SL network in recent KataGo releases"));
            ok = fail(in, &e, "Human network");
        } else {
            String file = string_format(SV_FMT "/networks/human/" SV_FMT, SV_ARG(string_sv(&rq->katago_dir)), SV_ARG(string_sv(&human.name)));
            ok = download_network(in, string_sv(&human.url), string_sv(&file), human.size, "human SL network");
            string_free(&file);
            download_asset_free(&human);
        }
    }
    string_free(&releases);
    mutex_lock(&in->lock);
    if (in->cancel && !in->state.failed) {
        in->state.failed = true;
        snprintf(in->state.error, sizeof(in->state.error), "cancelled");
    }
    if (!in->state.failed) snprintf(in->state.label, sizeof(in->state.label), "Done");
    in->state.running = false;
    in->state.finished = true;
    mutex_unlock(&in->lock);
    return 0;
}

void installer_start(Installer *in, StringView katago_dir, const KataGoBackend *backend, bool engine, u32 networks, bool human) {
    if (in->started && in->state.running) return;
    if (in->started) installer_finish(in);
    *in = (Installer){ 0 };
    mutex_init(&in->lock);
    in->request = (InstallRequest){ string_from(katago_dir), backend, engine, human, networks };
    in->state.running = true;
    in->state.total = -1;
    snprintf(in->state.label, sizeof(in->state.label), "Starting");
    in->started = thread_start(&in->thread, worker_main, in);
    if (!in->started) {
        in->state.running = false;
        in->state.failed = in->state.finished = true;
        snprintf(in->state.error, sizeof(in->state.error), "cannot start installer thread");
    }
}

InstallState installer_state(Installer *in) {
    if (!in->started) return in->state;
    mutex_lock(&in->lock);
    InstallState s = in->state;
    mutex_unlock(&in->lock);
    return s;
}

void installer_cancel(Installer *in) { in->cancel = true; }

void installer_finish(Installer *in) {
    if (!in->started) return;
    thread_join(&in->thread);
    mutex_destroy(&in->lock);
    string_free(&in->request.katago_dir);
    in->started = false;
}
