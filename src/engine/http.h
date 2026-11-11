// http.h - HTTPS downloads through the system `curl` (bundled with Windows 10+
// and virtually every Linux), so Gifu needs no TLS library of its own.
// Calls block; run them on a worker thread.
#pragma once

#include "base/str.h"

typedef void (*HttpProgressFn)(void *user, i64 done, i64 total);
typedef bool (*HttpCancelFn)(void *user);

// GET into memory (follows redirects; fails on HTTP errors).
NODISCARD bool http_get(StringView url, String *body, String *error);
// Size from a HEAD request (after redirects), or -1.
i64 http_content_length(StringView url);
// Downloads to `dest` via `dest.part` (renamed on success). `total` may be -1.
NODISCARD bool http_download(StringView url, StringView dest, i64 total, HttpProgressFn progress, HttpCancelFn cancelled,
                             void *user, String *error);
