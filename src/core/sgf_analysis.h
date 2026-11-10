// sgf_analysis.h - engine analysis stored by other Go programs, so their
// analysed games show graphs / reports here and ours open there.
//
//   LZ[] (Lizzie, LizzieYzy):
//     "<engine> <win% of the player who just moved> <visits> <score, side to move> <stdev>\n"
//     "move D4 visits 500 winrate 5404 prior 0 scoreMean 0.50 pv D4 ... info move ..."
//     (move winrates are side to move, x100 integers)
//   KT[] (KaTrain): three values, each base64(gzip(...)): ownership and policy as
//     half floats (empty here), then JSON {"moves": {...}, "root": {...}, "completed": true}
//     with Black's winrate / scoreLead; the root carries KTV[1.0].
// We only keep a summary (NodeEval: winrate, score, visits, top 3 moves), so
// exports carry the root values and the best move.
#pragma once

#include "core/game_tree.h"

bool lz_parse(StringView text, Stone to_play, i32 width, i32 height, NodeEval *out);
String lz_format(const NodeEval *eval, Stone to_play, i32 width, i32 height);
bool kt_parse(const StringView *values, i32 count, i32 width, i32 height, NodeEval *out);
void kt_format(const NodeEval *eval, i32 width, i32 height, String out[3]);

// Writes LZ[] and/or KT[] on every node with an evaluation (call before saving).
void game_tree_store_interop(GameTree *tree, bool lz, bool kt);
