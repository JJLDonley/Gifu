// sgf.h - Smart Game Format (FF[4]) reader / writer.
// Unknown properties are preserved so files round-trip losslessly.
#pragma once

#include "core/game_tree.h"

typedef struct SgfError {
    isize offset;
    i32 line;
    char message[128];
} SgfError;

// Parses the first game of a collection. Returns NULL on error.
GameTree *sgf_parse(StringView text, SgfError *error);
// Komi as written by Fox (x100, sometimes in Chinese "zi": KM[375] = 3.75 zi =
// 7.5 points, KM[650] = 6.5). Values under 100 are returned unchanged.
f64 sgf_normalize_komi(f64 komi);
// Rewrites a Fox-style KM[] on the root; true if it changed (also set on load
// as tree->komi_fixed).
bool sgf_fix_komi(GameTree *tree);
// Loads a file; non-UTF-8 files are converted using their CA[] charset
// (GB18030 when unspecified) and re-tagged CA[UTF-8].
GameTree *sgf_load_file(StringView path, SgfError *error);
// Same decoding for in-memory bytes (used by the loader and tests).
GameTree *sgf_parse_bytes(StringView bytes, SgfError *error);

String sgf_write(const GameTree *tree);
NODISCARD bool sgf_save_file(const GameTree *tree, StringView path);
