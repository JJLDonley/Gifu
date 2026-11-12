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
enum { NODE_EVAL_TOP = 32 };   // candidates kept per node (Hawk Eye's match may look past the first few)

// Which engine an evaluation comes from. Each one is stored apart (KataGo in CGE[],
// Leela in CGL[]) and never replaces another's; the tree shows one at a time.
typedef enum EvalSource { EVAL_KATAGO, EVAL_LEELA, EVAL_SOURCES } EvalSource;

typedef struct NodeEval {
    bool valid;
    f32 black_winrate;   // 0..1
    f32 black_score;     // score lead from Black's point of view
    i32 visits;
    i32 top_count;
    Point top[NODE_EVAL_TOP];       // engine's best moves for the side to play here, best first
    f32 top_share[NODE_EVAL_TOP];   // each one's share of the search (visits / root visits); 0 = unknown
    bool no_score;   // from an engine without score estimates (Leela): black_score is unknown
    u8 source;       // EvalSource (0 = KataGo)
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

    NodeEval eval;                    // the shown source's evaluation (GameTree.eval_source)
    NodeEval evals[EVAL_SOURCES];     // every engine's own
};

typedef struct GameTree {
    Arena arena;
    GameNode *root;
    GameNode *current;
    BoardRules rules;
    // Bumped by every change to nodes, properties or evaluations. Anything
    // derived from the tree (rank summary, reports...) keys its cache on it, so
    // there is nothing to keep in sync by hand.
    u64 revision;
    bool komi_fixed;   // the loader corrected a Fox-style KM[] (see sgf_fix_komi)
    EvalSource eval_source;   // whose evaluations GameNode.eval shows (graph, panels, reports)
} GameTree;

GameTree *game_tree_new(i32 width, i32 height, f64 komi);
// Evals persist in SGF as CGE[winrate,score,visits,top1 top2 top3] (SGF coords),
// e.g. CGE[0.4812,-1.3,1200,pd dp qq].
void game_node_store_eval(GameTree *tree, GameNode *node, const NodeEval *eval);   // eval + CGE[]
// In-memory evaluation update (CGE[] is written on save). Keeps the deeper of the
// stored and the new evaluation, so browsing with live analysis never replaces a
// game analysis made with more visits. Returns true if it changed.
bool game_node_update_eval(GameTree *tree, GameNode *node, const NodeEval *eval);
// Each source is kept apart: an update or store touches only evals[eval->source]
// (and GameNode.eval when that is the shown source).
// Shows `source`'s evaluations in GameNode.eval on every node.
void game_tree_set_eval_source(GameTree *tree, EvalSource source);
bool game_tree_has_evals(const GameTree *tree, EvalSource source);   // any node evaluated by it
const char *eval_source_name(EvalSource source);                       // "KataGo", "Leela"
bool node_eval_parse(StringView text, i32 width, i32 height, NodeEval *out);
// Writes CGE[] for every node with a valid eval (call before saving).
void game_tree_store_evals(GameTree *tree);


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
void game_node_remove_prop(GameTree *tree, GameNode *node, StringView ident);

// ---- Derived state ---------------------------------------------------------------
void game_tree_rebuild(GameTree *tree, GameNode *from);
i32 game_tree_width(const GameTree *tree);
i32 game_tree_height(const GameTree *tree);
f64 game_tree_komi(const GameTree *tree);
StringView game_tree_info(const GameTree *tree, StringView ident);   // root property

// ---- Navigation / editing ----------------------------------------------------------
// Plays at the current node: reuses a matching child or creates a new variation.
MoveError game_tree_play(GameTree *tree, Move move);
// Plays `move` after `at` (reusing an existing child with that move) without
// changing the current node. NULL (and *error) if illegal there.
GameNode *game_tree_play_at(GameTree *tree, GameNode *at, Move move, MoveError *error);
// Same, but always a new child (even if one with that move exists).
GameNode *game_tree_play_new(GameTree *tree, GameNode *at, Move move, MoveError *error);
bool game_tree_forward(GameTree *tree);
bool game_tree_back(GameTree *tree);
void game_tree_to_start(GameTree *tree);
void game_tree_to_end(GameTree *tree);
bool game_tree_next_variation(GameTree *tree);   // switch to next sibling
bool game_tree_prev_variation(GameTree *tree);
void game_tree_goto(GameTree *tree, GameNode *node);
bool game_tree_delete_node(GameTree *tree, GameNode *node);   // unlinks the subtree (not root)
// Unlinks `node`'s subtree from its parent (memory stays in the tree's arena, so it
// can be linked again: undo). Returns its index among the siblings, -1 for the root.
// A current node inside it moves to the parent.
i32 game_tree_unlink(GameTree *tree, GameNode *node);
// Links a subtree (made in this tree) under `parent` as child number `index`
// (clamped to the end).
void game_tree_link(GameTree *tree, GameNode *parent, GameNode *child, i32 index);

// Win-rate the player who made this node's move lost by it (0..1, positive =
// mistake), from the evals before and after. False when either is unknown.
bool game_node_winrate_loss(const GameNode *node, f32 *loss);
bool game_node_score_loss(const GameNode *node, f32 *loss);

i32 game_node_child_count(const GameNode *node);
i32 game_node_sibling_index(const GameNode *node);
// The "active line": root..current followed by first-children past current.
// Writes up to cap nodes, returns total length.
isize game_tree_active_line(const GameTree *tree, GameNode **out, isize cap);
