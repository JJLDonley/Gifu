// update.h - self-update from GitHub releases: the pure parts (unit-tested).
//
// Versions are [v]MAJOR.MINOR.PATCH[-pre.release] (semantic versioning order:
// 0.2.0-alpha.1 < 0.2.0-alpha.2 < 0.2.0-beta < 0.2.0). Stable users get releases;
// the alpha channel also gets pre-releases. A release is used only if it has an
// archive for this platform and a SHA256SUMS file listing it.
#pragma once

#include "base/str.h"

i32 version_compare(StringView a, StringView b);   // <0: a is older
bool version_is_prerelease(StringView v);

typedef enum UpdateChannel { UPDATE_CHANNEL_STABLE, UPDATE_CHANNEL_ALPHA } UpdateChannel;
// The setting updates.releases: "stable", "prerelease" (also "alpha"), or "auto":
// pre-releases when the running version is one, else stable.
UpdateChannel update_channel_from(StringView setting, StringView running_version);

typedef struct ReleaseInfo {
    String version;      // "0.2.0-alpha.1" (tag without the leading v)
    String notes;        // release body (Markdown)
    String page_url;     // html_url
    String asset_name, asset_url, sums_url;
    i64 asset_size;
    bool prerelease;
} ReleaseInfo;

// From GitHub's /releases JSON (an array, newest first): the newest non-draft release
// allowed on `channel` with an asset ending in `asset_suffix` and a SHA256SUMS asset.
bool release_pick(StringView releases_json, UpdateChannel channel, StringView asset_suffix, ReleaseInfo *out);
void release_info_free(ReleaseInfo *r);

// This build's package: "-windows-x64.zip", "-linux-x64.tar.xz", or "-x86_64.AppImage".
StringView update_asset_suffix(bool appimage);
// The hex digest listed for `file_name` in a sha256sum-style text ("<hex>  <name>").
bool sums_find(StringView sums, StringView file_name, char out_hex[65]);
