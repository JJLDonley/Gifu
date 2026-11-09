// game_tree.h - the move tree of a game (variations), backed by SGF properties.
//
// SGF properties are the source of truth for every node; the derived fields
// (move, board, to_play, move_number) are recomputed by game_tree_rebuild().
// All nodes and strings live in the tree's arena.
#pragma once

#include "base/arena.h"
#include "core/board.h"

typedef struct SgfProperty SgfProperty;
struct SgfProperty {
    SgfProperty *next;
    StringView ident;
    StringView *values;
    i32 value_count;
};

// Compact engine evaluation summary kept per node (for graphs / hawk-eye).
typedef struct NodeEval {
    bool valid;
    f32 black_winrate;   // 0..1
    f32 black_score;     // score lead from Black's point of view
    i32 visits;
} NodeEval;

typedef struct GameNode GameNode;
struct GameNode {
    GameNode *parent;
    GameNode *first_child;
    GameNode *last_child;
    GameNode *next_sibling;
    SgfProperty *props;
    SgfProperty *last_prop;

    // Derived from props (game_tree_rebuild):
    bool has_move;
    Move move;
    Stone to_play;
    i32 move_number;
    Board board;   // position after this node

    NodeEval eval;
};

typedef struct GameTree {
    Arena arena;
    GameNode *root;
    GameNode *current;
    BoardRules rules;
} GameTree;

GameTree *game_tree_new(i32 width, i32 height, f64 komi);

typedef struct NewGameSpec {
    i32 size;
    f64 komi;
    i32 handicap;
    StringView black;
    StringView white;
    StringView rules;   // SGF RU value, may be empty
} NewGameSpec;

// New game with handicap stones (AB + HA, White to play) and player names.
GameTree *game_tree_new_game(const NewGameSpec *spec);
GameTree *game_tree_new_empty(void);   // root without properties (used by the SGF parser)
void game_tree_free(GameTree *tree);

GameNode *game_tree_append_child(GameTree *tree, GameNode *parent);

// ---- Properties ------------------------------------------------------------------
SgfProperty *game_node_find_prop(const GameNode *node, StringView ident);
StringView game_node_prop(const GameNode *node, StringView ident);   // first value or ""
void game_node_set_prop(GameTree *tree, GameNode *node, StringView ident, StringView value);
void game_node_add_prop_value(GameTree *tree, GameNode *node, StringView ident, StringView value);
void game_node_remove_prop(GameNode *node, StringView ident);

// ---- Derived state ---------------------------------------------------------------
void game_tree_rebuild(GameTree *tree, GameNode *from);
i32 game_tree_width(const GameTree *tree);
i32 game_tree_height(const GameTree *tree);
f64 game_tree_komi(const GameTree *tree);
StringView game_tree_info(const GameTree *tree, StringView ident);   // root property

// ---- Navigation / editing ----------------------------------------------------------
// Plays at the current node: reuses a matching child or creates a new variation.
MoveError game_tree_play(GameTree *tree, Move move);
bool game_tree_forward(GameTree *tree);
bool game_tree_back(GameTree *tree);
void game_tree_to_start(GameTree *tree);
void game_tree_to_end(GameTree *tree);
bool game_tree_next_variation(GameTree *tree);   // switch to next sibling
bool game_tree_prev_variation(GameTree *tree);
void game_tree_goto(GameTree *tree, GameNode *node);
bool game_tree_delete_node(GameTree *tree, GameNode *node);   // unlinks the subtree (not root)

i32 game_node_child_count(const GameNode *node);
i32 game_node_sibling_index(const GameNode *node);
// The "active line": root..current followed by first-children past current.
// Writes up to cap nodes, returns total length.
isize game_tree_active_line(const GameTree *tree, GameNode **out, isize cap);
