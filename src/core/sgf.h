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
GameTree *sgf_load_file(StringView path, SgfError *error);

String sgf_write(const GameTree *tree);
NODISCARD bool sgf_save_file(const GameTree *tree, StringView path);
