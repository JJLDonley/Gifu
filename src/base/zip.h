// zip.h - minimal zip extraction (stored + deflate, no zip64/encryption),
// enough for KataGo release archives. Entry paths that would escape the
// destination ("../", absolute paths) are rejected.
#pragma once

#include "base/str.h"

typedef struct ZipStats {
    i32 files;
    i64 bytes;
} ZipStats;

// Extracts every file of `zip_path` into `dest_dir` (created as needed).
NODISCARD bool zip_extract(StringView zip_path, StringView dest_dir, ZipStats *stats, String *error);
// Same, from memory.
NODISCARD bool zip_extract_memory(StringView data, StringView dest_dir, ZipStats *stats, String *error);
