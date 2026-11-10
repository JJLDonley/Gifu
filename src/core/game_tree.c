#include "core/game_tree.h"
#include "core/sgf_analysis.h"

#include "base/log.h"

#include <stdio.h>
#include <stdlib.h>

// ---- Construction ---------------------------------------------------------------

GameTree *game_tree_new_empty(void) {
    GameTree *tree = calloc(1, sizeof(GameTree));
    if (!tree) abort();
    tree->arena.default_block_size = 256 * 1024;
    tree->rules = (BoardRules){ .allow_suicide = false };
    tree->root = arena_new(&tree->arena, GameNode);
    tree->current = tree->root;
    return tree;
}

GameTree *game_tree_new(i32 width, i32 height, f64 komi) {
    GameTree *tree = game_tree_new_empty();
    char buffer[32];
    game_node_set_prop(tree, tree->root, SV("GM"), SV("1"));
    game_node_set_prop(tree, tree->root, SV("FF"), SV("4"));
    game_node_set_prop(tree, tree->root, SV("CA"), SV("UTF-8"));
    game_node_set_prop(tree, tree->root, SV("AP"), SV("CataGui:0.1"));
    if (width == height) snprintf(buffer, sizeof(buffer), "%d", width);
    else snprintf(buffer, sizeof(buffer), "%d:%d", width, height);
    game_node_set_prop(tree, tree->root, SV("SZ"), sv(buffer));
    snprintf(buffer, sizeof(buffer), "%g", komi);
    game_node_set_prop(tree, tree->root, SV("KM"), sv(buffer));
    game_tree_rebuild(tree, tree->root);
    return tree;
}

GameTree *game_tree_new_game(const NewGameSpec *spec) {
    GameTree *tree = game_tree_new(spec->size, spec->size, spec->komi);
    GameNode *root = tree->root;
    if (spec->black.len) game_node_set_prop(tree, root, SV("PB"), spec->black);
    if (spec->white.len) game_node_set_prop(tree, root, SV("PW"), spec->white);
    if (spec->rules.len) game_node_set_prop(tree, root, SV("RU"), spec->rules);
    Point stones[9];
    i32 n = board_handicap_points(spec->size, spec->size, spec->handicap, stones);
    if (n > 0) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%d", n);
        game_node_set_prop(tree, root, SV("HA"), sv(buffer));
        for (i32 i = 0; i < n; ++i) {
            char coord[3];
            point_to_sgf(stones[i], coord);
            game_node_add_prop_value(tree, root, SV("AB"), sv(coord));
        }
    }
    game_tree_rebuild(tree, root);
    return tree;
}

void game_tree_free(GameTree *tree) {
    if (!tree) return;
    arena_free(&tree->arena);
    free(tree);
}

GameNode *game_tree_append_child(GameTree *tree, GameNode *parent) {
    GameNode *node = arena_new(&tree->arena, GameNode);
    node->parent = parent;
    if (parent->last_child) parent->last_child->next_sibling = node;
    else parent->first_child = node;
    parent->last_child = node;
    return node;
}

// ---- Properties -----------------------------------------------------------------

SgfProperty *game_node_find_prop(const GameNode *node, StringView ident) {
    for (SgfProperty *p = node->props; p; p = p->next) {
        if (sv_eq(p->ident, ident)) return p;
    }
    return NULL;
}

StringView game_node_prop(const GameNode *node, StringView ident) {
    SgfProperty *p = game_node_find_prop(node, ident);
    return p && p->value_count > 0 ? p->values[0] : SV("");
}

static SgfProperty *new_prop(GameTree *tree, GameNode *node, StringView ident) {
    SgfProperty *p = arena_new(&tree->arena, SgfProperty);
    p->ident = arena_copy_sv(&tree->arena, ident);
    if (node->last_prop) node->last_prop->next = p;
    else node->props = p;
    node->last_prop = p;
    return p;
}

void game_node_set_prop(GameTree *tree, GameNode *node, StringView ident, StringView value) {
    SgfProperty *p = game_node_find_prop(node, ident);
    if (!p) p = new_prop(tree, node, ident);
    p->values = arena_new_array(&tree->arena, StringView, 1);
    p->values[0] = arena_copy_sv(&tree->arena, value);
    p->value_count = 1;
}

void game_node_add_prop_value(GameTree *tree, GameNode *node, StringView ident, StringView value) {
    SgfProperty *p = game_node_find_prop(node, ident);
    if (!p) p = new_prop(tree, node, ident);
    StringView *values = arena_new_array(&tree->arena, StringView, p->value_count + 1);
    for (i32 i = 0; i < p->value_count; ++i) values[i] = p->values[i];
    values[p->value_count] = arena_copy_sv(&tree->arena, value);
    p->values = values;
    p->value_count++;
}

void game_node_remove_prop(GameNode *node, StringView ident) {
    SgfProperty *prev = NULL;
    for (SgfProperty *p = node->props; p; prev = p, p = p->next) {
        if (!sv_eq(p->ident, ident)) continue;
        if (prev) prev->next = p->next;
        else node->props = p->next;
        if (node->last_prop == p) node->last_prop = prev;
        return;
    }
}

// ---- Derived state --------------------------------------------------------------

static void parse_size(StringView sz, i32 *width, i32 *height) {
    i64 w = 19, h = 19;
    StringView rest = sz;
    StringView first = sv_chop_char(&rest, ':');
    if (!sv_parse_i64(first, &w)) w = 19;
    if (rest.len == 0 || !sv_parse_i64(rest, &h)) h = w;
    *width = (i32)CLAMP(w, 2, BOARD_MAX_SIZE);
    *height = (i32)CLAMP(h, 2, BOARD_MAX_SIZE);
}

// Applies AB/AW/AE values, including compressed "aa:cc" rectangles.
static void apply_setup(Board *board, const SgfProperty *prop, Stone stone) {
    for (i32 i = 0; i < prop->value_count; ++i) {
        StringView v = prop->values[i];
        StringView second = v;
        StringView first = sv_chop_char(&second, ':');
        Point a, b;
        if (!point_from_sgf(first, board->width, board->height, &a) || point_is_pass(a)) continue;
        b = a;
        if (second.len > 0 && (!point_from_sgf(second, board->width, board->height, &b) || point_is_pass(b))) {
            continue;
        }
        for (i32 y = MIN(a.y, b.y); y <= MAX(a.y, b.y); ++y) {
            for (i32 x = MIN(a.x, b.x); x <= MAX(a.x, b.x); ++x) board_set(board, (Point){ (i8)x, (i8)y }, stone);
        }
    }
}

static void compute_node(GameTree *tree, GameNode *node) {
    Board *board = &node->board;
    if (node->parent) {
        *board = node->parent->board;
        node->move_number = node->parent->move_number;
        node->to_play = node->parent->to_play;
    } else {
        i32 w, h;
        parse_size(game_node_prop(node, SV("SZ")), &w, &h);
        board_init(board, w, h);
        node->move_number = 0;
        node->to_play = STONE_BLACK;
    }

    bool has_setup = false;
    node->has_move = false;
    for (SgfProperty *p = node->props; p; p = p->next) {
        if (sv_eq(p->ident, SV("AB"))) apply_setup(board, p, STONE_BLACK), has_setup = true;
        else if (sv_eq(p->ident, SV("AW"))) apply_setup(board, p, STONE_WHITE), has_setup = true;
        else if (sv_eq(p->ident, SV("AE"))) apply_setup(board, p, STONE_EMPTY), has_setup = true;
    }
    if (!node->parent && has_setup && board_stone_count(board, STONE_WHITE) == 0 &&
        board_stone_count(board, STONE_BLACK) > 0) {
        node->to_play = STONE_WHITE;   // handicap stones
    }

    for (SgfProperty *p = node->props; p; p = p->next) {
        bool is_black = sv_eq(p->ident, SV("B"));
        if (!is_black && !sv_eq(p->ident, SV("W"))) continue;
        Point pt;
        StringView value = p->value_count > 0 ? p->values[0] : SV("");
        if (!point_from_sgf(value, board->width, board->height, &pt)) continue;
        node->has_move = true;
        node->move = (Move){ is_black ? STONE_BLACK : STONE_WHITE, pt };
        BoardRules replay = tree->rules;
        replay.ignore_ko = true;
        replay.allow_suicide = true;
        MoveError err = board_play(board, node->move, replay);
        if (err != MOVE_OK) log_warn("SGF move " SV_FMT " ignored: %s", SV_ARG(value), move_error_name(err));
        node->move_number++;
        node->to_play = stone_opponent(node->move.color);
        break;
    }

    StringView cge = game_node_prop(node, SV("CGE"));
    if (cge.len && !node->eval.valid) node_eval_parse(cge, board->width, board->height, &node->eval);

    StringView pl = game_node_prop(node, SV("PL"));
    if (sv_eq(pl, SV("B")) || sv_eq(pl, SV("b"))) node->to_play = STONE_BLACK;
    if (sv_eq(pl, SV("W")) || sv_eq(pl, SV("w"))) node->to_play = STONE_WHITE;

    // No CGE[]: analysis written by KaTrain (KT[]) or Lizzie / LizzieYzy (LZ[]).
    if (!node->eval.valid) {
        SgfProperty *kt = game_node_find_prop(node, SV("KT"));
        if (kt) kt_parse(kt->values, kt->value_count, board->width, board->height, &node->eval);
    }
    if (!node->eval.valid) {
        StringView lz = game_node_prop(node, SV("LZ"));
        if (lz.len) lz_parse(lz, node->to_play, board->width, board->height, &node->eval);
    }
}

void game_tree_rebuild(GameTree *tree, GameNode *from) {
    if (!from) from = tree->root;
    // Iterative pre-order walk of the subtree rooted at `from`.
    GameNode *node = from;
    while (node) {
        compute_node(tree, node);
        if (node->first_child) {
            node = node->first_child;
            continue;
        }
        while (node && node != from && !node->next_sibling) node = node->parent;
        if (!node || node == from) break;
        node = node->next_sibling;
    }
}

i32 game_tree_width(const GameTree *tree) { return tree->root->board.width; }
i32 game_tree_height(const GameTree *tree) { return tree->root->board.height; }

f64 game_tree_komi(const GameTree *tree) {
    f64 komi = 7.5;
    StringView km = game_node_prop(tree->root, SV("KM"));
    if (km.len > 0 && !sv_parse_f64(km, &komi)) komi = 7.5;
    return komi;
}

StringView game_tree_info(const GameTree *tree, StringView ident) { return game_node_prop(tree->root, ident); }

// ---- Navigation / editing -----------------------------------------------------------

MoveError game_tree_play(GameTree *tree, Move move) {
    GameNode *cur = tree->current;
    MoveError err = board_check(&cur->board, move, tree->rules);
    if (err != MOVE_OK) return err;
    for (GameNode *c = cur->first_child; c; c = c->next_sibling) {
        if (c->has_move && c->move.color == move.color && point_eq(c->move.point, move.point)) {
            tree->current = c;
            return MOVE_OK;
        }
    }
    GameNode *node = game_tree_append_child(tree, cur);
    char coord[3];
    point_to_sgf(move.point, coord);
    game_node_set_prop(tree, node, move.color == STONE_BLACK ? SV("B") : SV("W"), sv(coord));
    compute_node(tree, node);
    tree->current = node;
    return MOVE_OK;
}

bool game_tree_forward(GameTree *tree) {
    if (!tree->current->first_child) return false;
    tree->current = tree->current->first_child;
    return true;
}

bool game_tree_back(GameTree *tree) {
    if (!tree->current->parent) return false;
    tree->current = tree->current->parent;
    return true;
}

void game_tree_to_start(GameTree *tree) { tree->current = tree->root; }

void game_tree_to_end(GameTree *tree) {
    while (game_tree_forward(tree)) {}
}

bool game_tree_next_variation(GameTree *tree) {
    GameNode *cur = tree->current;
    if (!cur->next_sibling) return false;
    tree->current = cur->next_sibling;
    return true;
}

bool game_tree_prev_variation(GameTree *tree) {
    GameNode *cur = tree->current;
    if (!cur->parent || cur->parent->first_child == cur) return false;
    GameNode *prev = cur->parent->first_child;
    while (prev->next_sibling != cur) prev = prev->next_sibling;
    tree->current = prev;
    return true;
}

void game_tree_goto(GameTree *tree, GameNode *node) {
    if (node) tree->current = node;
}

bool game_tree_delete_node(GameTree *tree, GameNode *node) {
    GameNode *parent = node->parent;
    if (!parent) return false;
    GameNode *prev = NULL;
    for (GameNode *c = parent->first_child; c; prev = c, c = c->next_sibling) {
        if (c != node) continue;
        if (prev) prev->next_sibling = c->next_sibling;
        else parent->first_child = c->next_sibling;
        if (parent->last_child == c) parent->last_child = prev;
        break;
    }
    for (GameNode *n = tree->current; n; n = n->parent) {
        if (n == node) {
            tree->current = parent;
            break;
        }
    }
    return true;
}

bool node_eval_parse(StringView text, i32 width, i32 height, NodeEval *out) {
    NodeEval e = { 0 };
    StringView rest = text;
    f64 wr = 0, score = 0;
    i64 visits = 0;
    if (!sv_parse_f64(sv_chop_char(&rest, ','), &wr) || !sv_parse_f64(sv_chop_char(&rest, ','), &score) ||
        !sv_parse_i64(sv_chop_char(&rest, ','), &visits)) {
        return false;
    }
    e.valid = true;
    e.black_winrate = (f32)CLAMP(wr, 0.0, 1.0);
    e.black_score = (f32)score;
    e.visits = (i32)visits;
    // "pd dd" (older files) or "pd:0.452 dd:0.201" (move:share of the search).
    for (StringView tok = sv_chop_token(&rest); tok.len && e.top_count < NODE_EVAL_TOP; tok = sv_chop_token(&rest)) {
        StringView coord = sv_chop_char(&tok, ':');
        Point p;
        if (!point_from_sgf(coord, width, height, &p)) continue;
        f64 share = 0;
        e.top_share[e.top_count] = tok.len && sv_parse_f64(tok, &share) ? (f32)CLAMP(share, 0.0, 1.0) : 0.0f;
        e.top[e.top_count++] = p;
    }
    *out = e;
    return true;
}

void game_node_store_eval(GameTree *tree, GameNode *node, const NodeEval *eval) {
    node->eval = *eval;
    char buffer[256];
    i32 n = snprintf(buffer, sizeof(buffer), "%.4f,%.2f,%d,", eval->black_winrate, eval->black_score, eval->visits);
    for (i32 i = 0; i < eval->top_count && n < (i32)sizeof(buffer) - 16; ++i) {
        char coord[3];
        point_to_sgf(eval->top[i], coord);
        n += snprintf(buffer + n, sizeof(buffer) - (usize)n, "%s%s", i ? " " : "", point_is_pass(eval->top[i]) ? "tt" : coord);
        if (eval->top_share[i] > 0) n += snprintf(buffer + n, sizeof(buffer) - (usize)n, ":%.3f", eval->top_share[i]);
    }
    game_node_set_prop(tree, node, SV("CGE"), sv(buffer));
}

void game_tree_store_evals(GameTree *tree) {
    GameNode *node = tree->root;
    while (node) {
        if (node->eval.valid) {
            NodeEval e = node->eval;
            game_node_store_eval(tree, node, &e);
        }
        if (node->first_child) {
            node = node->first_child;
            continue;
        }
        while (node && !node->next_sibling) node = node->parent;
        if (node) node = node->next_sibling;
    }
}

bool game_node_winrate_loss(const GameNode *node, f32 *loss) {
    if (!node->has_move || !node->parent || !node->eval.valid || !node->parent->eval.valid) return false;
    f32 before = node->parent->eval.black_winrate, after = node->eval.black_winrate;
    *loss = node->move.color == STONE_BLACK ? before - after : after - before;
    return true;
}

bool game_node_score_loss(const GameNode *node, f32 *loss) {
    if (!node->has_move || !node->parent || !node->eval.valid || !node->parent->eval.valid) return false;
    f32 before = node->parent->eval.black_score, after = node->eval.black_score;
    *loss = node->move.color == STONE_BLACK ? before - after : after - before;
    return true;
}

i32 game_node_child_count(const GameNode *node) {
    i32 n = 0;
    for (GameNode *c = node->first_child; c; c = c->next_sibling) ++n;
    return n;
}

i32 game_node_sibling_index(const GameNode *node) {
    if (!node->parent) return 0;
    i32 i = 0;
    for (GameNode *c = node->parent->first_child; c && c != node; c = c->next_sibling) ++i;
    return i;
}

isize game_tree_active_line(const GameTree *tree, GameNode **out, isize cap) {
    isize depth = 0;
    for (GameNode *n = tree->current; n; n = n->parent) ++depth;
    isize total = depth;
    for (GameNode *n = tree->current->first_child; n; n = n->first_child) ++total;
    isize i = depth - 1;
    for (GameNode *n = tree->current; n; n = n->parent, --i) {
        if (i < cap) out[i] = n;
    }
    i = depth;
    for (GameNode *n = tree->current->first_child; n; n = n->first_child, ++i) {
        if (i < cap) out[i] = n;
    }
    return total;
}
