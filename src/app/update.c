#include "app/update.h"

#include "platform/os.h"

#include <stdlib.h>

#include "base/json.h"

#include <string.h>

// ---- Versions -----------------------------------------------------------------------------------

static StringView strip_v(StringView v) {
    v = sv_trim(v);
    return v.len && (v.data[0] == 'v' || v.data[0] == 'V') ? sv_drop(v, 1) : v;
}

static bool all_digits(StringView s) {
    if (s.len == 0) return false;
    for (isize i = 0; i < s.len; ++i) {
        if (s.data[i] < '0' || s.data[i] > '9') return false;
    }
    return true;
}

static i32 cmp_i64(i64 a, i64 b) { return a < b ? -1 : a > b; }

i32 version_compare(StringView a, StringView b) {
    a = strip_v(a), b = strip_v(b);
    isize da = sv_find_char(a, '-'), db = sv_find_char(b, '-');
    StringView core_a = da >= 0 ? sv_take(a, da) : a, core_b = db >= 0 ? sv_take(b, db) : b;
    StringView pre_a = da >= 0 ? sv_drop(a, da + 1) : SV(""), pre_b = db >= 0 ? sv_drop(b, db + 1) : SV("");
    for (i32 k = 0; k < 3; ++k) {   // MAJOR.MINOR.PATCH (missing parts are 0)
        i64 x = 0, y = 0;
        StringView pa = sv_chop_char(&core_a, '.'), pb = sv_chop_char(&core_b, '.');
        if (pa.len && !sv_parse_i64(pa, &x)) x = 0;
        if (pb.len && !sv_parse_i64(pb, &y)) y = 0;
        if (x != y) return cmp_i64(x, y);
    }
    // A release is newer than any of its pre-releases.
    if (!pre_a.len || !pre_b.len) return pre_a.len ? -1 : pre_b.len ? 1 : 0;
    while (pre_a.len || pre_b.len) {   // dot-separated identifiers
        if (!pre_a.len) return -1;     // fewer identifiers sort first
        if (!pre_b.len) return 1;
        StringView ia = sv_chop_char(&pre_a, '.'), ib = sv_chop_char(&pre_b, '.');
        bool na = all_digits(ia), nb = all_digits(ib);
        if (na && nb) {
            i64 x = 0, y = 0;
            if (!sv_parse_i64(ia, &x)) x = 0;   // all digits: cannot fail short of overflow
            if (!sv_parse_i64(ib, &y)) y = 0;
            if (x != y) return cmp_i64(x, y);
        } else if (na != nb) {
            return na ? -1 : 1;   // numeric identifiers sort before alphanumeric ones
        } else {
            isize n = MIN(ia.len, ib.len);
            i32 c = memcmp(ia.data, ib.data, (usize)n);
            if (c) return c < 0 ? -1 : 1;
            if (ia.len != ib.len) return ia.len < ib.len ? -1 : 1;
        }
    }
    return 0;
}

bool version_is_prerelease(StringView v) { return sv_find_char(strip_v(v), '-') >= 0; }

// ---- Releases -----------------------------------------------------------------------------------

StringView update_asset_suffix(bool appimage) {
    StringView platform = sv(os_platform_name());
    if (sv_eq(platform, SV("windows"))) return SV("-windows-x64.zip");
    if (sv_eq(platform, SV("macos"))) return SV("-macos.zip");   // Gifu.app (Apple Silicon and Intel)
    return appimage ? SV("-x86_64.AppImage") : SV("-linux-x64.tar.xz");
}

void release_info_free(ReleaseInfo *r) {
    string_free(&r->version);
    string_free(&r->notes);
    string_free(&r->page_url);
    string_free(&r->asset_name);
    string_free(&r->asset_url);
    string_free(&r->sums_url);
    *r = (ReleaseInfo){ 0 };
}

static i32 newest_first(const void *a, const void *b) {
    const ReleaseInfo *x = a, *y = b;
    return version_compare(string_sv(&y->version), string_sv(&x->version));
}

bool release_list(StringView json, StringView suffix, ReleaseVec *out) {
    *out = (ReleaseVec){ 0 };
    JsonDoc doc = { 0 };
    if (!json_parse(json, &doc, NULL)) return false;
    JsonValue list = json_root(&doc);
    for (isize i = 0; i < json_array_len(list); ++i) {
        JsonValue rel = json_array_at(list, i);
        if (json_bool(json_get(rel, "draft"), false)) continue;
        StringView version = strip_v(json_string(json_get(rel, "tag_name"), SV("")));
        if (!version.len) continue;
        JsonValue assets = json_get(rel, "assets");
        StringView asset = SV(""), asset_url = SV(""), sums_url = SV("");
        i64 size = 0;
        for (isize k = 0; k < json_array_len(assets); ++k) {
            JsonValue a = json_array_at(assets, k);
            StringView name = json_string(json_get(a, "name"), SV(""));
            StringView url = json_string(json_get(a, "browser_download_url"), SV(""));
            if (sv_ends_with(name, suffix)) asset = name, asset_url = url, size = json_int(json_get(a, "size"), 0);
            if (sv_eq(name, SV("SHA256SUMS"))) sums_url = url;
        }
        if (!asset.len || !sums_url.len) continue;   // not for this platform, or unverifiable
        ReleaseInfo r = {
            .version = string_from(version),
            .notes = string_from(json_string(json_get(rel, "body"), SV(""))),
            .page_url = string_from(json_string(json_get(rel, "html_url"), SV(""))),
            .asset_name = string_from(asset),
            .asset_url = string_from(asset_url),
            .sums_url = string_from(sums_url),
            .asset_size = size,
            .prerelease = json_bool(json_get(rel, "prerelease"), false),
        };
        vec_push(out, r);
    }
    json_free(&doc);
    if (out->len > 1) qsort(out->items, (usize)out->len, sizeof(ReleaseInfo), newest_first);
    return true;
}

void release_vec_free(ReleaseVec *v) {
    vec_foreach(ReleaseInfo, r, v) release_info_free(r);
    vec_free(v);
}

ReleaseInfo release_info_copy(const ReleaseInfo *r) {
    return (ReleaseInfo){
        .version = string_from(string_sv(&r->version)),
        .notes = string_from(string_sv(&r->notes)),
        .page_url = string_from(string_sv(&r->page_url)),
        .asset_name = string_from(string_sv(&r->asset_name)),
        .asset_url = string_from(string_sv(&r->asset_url)),
        .sums_url = string_from(string_sv(&r->sums_url)),
        .asset_size = r->asset_size,
        .prerelease = r->prerelease,
    };
}

bool release_pick(StringView json, UpdateChannel channel, StringView suffix, ReleaseInfo *out) {
    *out = (ReleaseInfo){ 0 };
    ReleaseVec all;
    if (!release_list(json, suffix, &all)) return false;
    bool found = false;
    vec_foreach(ReleaseInfo, r, &all) {
        if (r->prerelease && channel != UPDATE_CHANNEL_ALPHA) continue;
        *out = release_info_copy(r);
        found = true;
        break;
    }
    release_vec_free(&all);
    return found;
}

bool sums_find(StringView sums, StringView file, char out[65]) {
    StringView rest = sums;
    while (rest.len) {
        StringView line = sv_trim(sv_chop_char(&rest, '\n'));
        isize space = sv_find_char(line, ' ');
        if (space != 64) continue;
        StringView name = sv_trim(sv_drop(line, space));
        if (name.len && name.data[0] == '*') name = sv_drop(name, 1);   // binary-mode marker
        if (!sv_eq(name, file)) continue;
        for (i32 i = 0; i < 64; ++i) {
            char c = line.data[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
            out[i] = (char)(c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c);
        }
        out[64] = '\0';
        return true;
    }
    return false;
}

UpdateChannel update_channel_from(StringView setting, StringView running) {
    if (sv_eq(setting, SV("stable"))) return UPDATE_CHANNEL_STABLE;
    if (sv_eq(setting, SV("prerelease")) || sv_eq(setting, SV("alpha"))) return UPDATE_CHANNEL_ALPHA;
    return version_is_prerelease(running) ? UPDATE_CHANNEL_ALPHA : UPDATE_CHANNEL_STABLE;   // auto
}
