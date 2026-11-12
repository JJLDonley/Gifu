// move_edit.h - editing a move in the middle of a game, like LizzieYzy's "Insert
// black / white" and its stone menu (Delete, Exchange, Move, drag a stone):
// the line from the edited move to the end is played again with the change.
//
// The line is the one being viewed: the edited move down to the current node,
// then the main continuation. Moves that become illegal are skipped (counted).
// Comments and markup travel with their moves; analysis does not (the positions
// changed). The rewritten branch takes the old one's place among its siblings;
// the old branch is only unlinked, so move_edit_undo can put it back (with its
// analysis).
#pragma once

#include "core/game_tree.h"

typedef enum MoveEditKind {
    MOVE_EDIT_INSERT,     // a stone after `node` (the current one); the rest follows it; the
                          // same player is still to move (PL[])
    MOVE_EDIT_DELETE,     // remove `node`'s move ("de-insert")
    MOVE_EDIT_SWAP,       // `node`'s move by the other colour
    MOVE_EDIT_RELOCATE,   // `node`'s move to `move.point`
} MoveEditKind;

typedef struct MoveEdit {
    MoveEditKind kind;
    GameNode *node;
    Move move;   // INSERT: the move; RELOCATE: the new point (colour ignored)
} MoveEdit;

// What an edit changed (and what undo needs).
typedef struct MoveEditRecord {
    GameNode *parent;      // where the branches hang
    GameNode *old_child;   // the replaced branch (unlinked), or NULL
    GameNode *new_child;   // the rewritten branch, or NULL (everything after was dropped)
    i32 index;             // their place among parent's children
    GameNode *current_before, *current_after;
    i32 skipped;           // later moves that became illegal and were left out
} MoveEditRecord;

// Applies `edit`; false (nothing changed) when it is not possible: the move is
// illegal there, the node has no move, or the node is not on the viewed line.
bool move_edit_apply(GameTree *tree, const MoveEdit *edit, MoveEditRecord *out);
// Puts the old branch back (and the current node), or redoes the edit.
void move_edit_undo(GameTree *tree, const MoveEditRecord *r);
void move_edit_redo(GameTree *tree, const MoveEditRecord *r);
// The node whose move put the stone at `p` on the current board (the latest such
// move up to the current node), or NULL (empty point, or a setup stone).
GameNode *move_edit_stone_node(const GameTree *tree, Point p);
