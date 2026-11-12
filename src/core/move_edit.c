#include "core/move_edit.h"

#include <stdlib.h>

// Properties that travel with a move when its line is played again: the player to
// move after an inserted stone (PL), comments and markup (not the move itself, nor analysis of a position that no longer exists).
static bool carried(StringView ident) {
    static const char *const KEEP[] = { "PL", "C", "N", "LB", "TR", "SQ", "CR", "MA", "TB", "TW", "AR", "LN", "CGS", "CGP" };
    for (i32 i = 0; i < countof(KEEP); ++i) {
        if (sv_eq(ident, sv(KEEP[i]))) return true;
    }
    return false;
}

static void copy_props(GameTree *tree, const GameNode *from, GameNode *to, bool all) {
    for (const SgfProperty *p = from->props; p; p = p->next) {
        if (!all && !carried(p->ident)) continue;
        for (i32 i = 0; i < p->value_count; ++i) game_node_add_prop_value(tree, to, p->ident, p->values[i]);
    }
}

static bool on_path_to(const GameNode *node, const GameNode *descendant) {
    for (const GameNode *n = descendant; n; n = n->parent) {
        if (n == node) return true;
    }
    return false;
}

bool move_edit_apply(GameTree *tree, const MoveEdit *e, MoveEditRecord *out) {
    *out = (MoveEditRecord){ .current_before = tree->current, .index = -1 };
    GameNode *cur = tree->current;
    GameNode *parent, *first;   // the line to play again starts at `first` (child of `parent`)
    if (e->kind == MOVE_EDIT_INSERT) {
        parent = e->node;
        first = parent->first_child;
        if (parent != cur) return false;
    } else {
        if (!e->node || !e->node->parent || !e->node->has_move || !on_path_to(e->node, cur)) return false;
        parent = e->node->parent;
        first = e->node;
    }
    // The viewed line from `first`: down to the current node, then first children.
    enum { MAX_LINE = 2048 };
    GameNode **line = malloc(sizeof(GameNode *) * MAX_LINE);
    if (!line) abort();
    isize count = 0;
    if (first) {
        GameNode *path[MAX_LINE];
        isize up = 0;
        if (on_path_to(first, cur)) {
            for (GameNode *n = cur; n && n != parent && up < MAX_LINE; n = n->parent) path[up++] = n;
            for (isize i = up - 1; i >= 0; --i) line[count++] = path[i];
            for (GameNode *n = cur->first_child; n && count < MAX_LINE; n = n->first_child) line[count++] = n;
        } else {
            for (GameNode *n = first; n && count < MAX_LINE; n = n->first_child) line[count++] = n;
        }
    }
    // The new branch.
    GameNode *at = parent, *focus = NULL, *new_first = NULL;
    i32 skipped = 0;
    bool ok = true;
    if (first) out->index = game_tree_unlink(tree, first);
    if (e->kind == MOVE_EDIT_INSERT) {
        MoveError err;
        GameNode *n = game_tree_play_new(tree, at, e->move, &err);
        if (!n) ok = false;
        else at = n, focus = n, new_first = n;
        // An inserted stone does not change whose turn it is (PL[] says so).
        if (n && n->to_play != parent->to_play) {
            game_node_set_prop(tree, n, SV("PL"), parent->to_play == STONE_BLACK ? SV("B") : SV("W"));
            game_tree_rebuild(tree, n);
        }
    }
    for (isize i = 0; ok && i < count; ++i) {
        const GameNode *old = line[i];
        GameNode *made = NULL;
        if (old->has_move) {
            Move m = old->move;
            bool edited = i == 0 && e->kind != MOVE_EDIT_INSERT;
            if (edited && e->kind == MOVE_EDIT_DELETE) {
                if (old == cur) focus = at;   // was looking at the deleted move: show the one before
                continue;
            }
            if (edited && e->kind == MOVE_EDIT_SWAP) m.color = m.color == STONE_BLACK ? STONE_WHITE : STONE_BLACK;
            if (edited && e->kind == MOVE_EDIT_RELOCATE) m.point = e->move.point;
            MoveError err;
            made = game_tree_play_new(tree, at, m, &err);
            if (!made) {
                if (edited) {   // the edit itself is illegal: undo everything
                    ok = false;
                    break;
                }
                ++skipped;
                if (old == cur && !focus) focus = at;
                continue;
            }
            copy_props(tree, old, made, false);
            if (game_node_find_prop(made, SV("PL"))) game_tree_rebuild(tree, made);   // whose turn it is
        } else {   // a node without a move (setup, comment): copied as is
            made = game_tree_append_child(tree, at);
            copy_props(tree, old, made, true);
            game_tree_rebuild(tree, made);
        }
        if (!new_first) new_first = made;
        at = made;
        if (old == cur && !focus) focus = made;
    }
    free(line);
    if (!ok) {   // put things back as they were
        if (new_first) game_tree_unlink(tree, new_first);
        if (first) game_tree_link(tree, parent, first, out->index);
        tree->current = out->current_before;
        return false;
    }
    // The new branch takes the old one's place among the siblings.
    if (new_first && first) {
        game_tree_unlink(tree, new_first);
        game_tree_link(tree, parent, new_first, out->index);
    } else if (new_first) {
        out->index = game_node_sibling_index(new_first);
    }
    if (!focus) focus = e->kind == MOVE_EDIT_INSERT ? new_first : (new_first ? new_first : parent);
    game_tree_goto(tree, focus);
    *out = (MoveEditRecord){ parent, first, new_first, out->index, out->current_before, focus, skipped };
    tree->revision++;
    return true;
}

void move_edit_undo(GameTree *tree, const MoveEditRecord *r) {
    if (r->new_child) game_tree_unlink(tree, r->new_child);
    if (r->old_child) game_tree_link(tree, r->parent, r->old_child, r->index);
    game_tree_goto(tree, r->current_before);
}

void move_edit_redo(GameTree *tree, const MoveEditRecord *r) {
    if (r->old_child) game_tree_unlink(tree, r->old_child);
    if (r->new_child) game_tree_link(tree, r->parent, r->new_child, r->index);
    game_tree_goto(tree, r->current_after);
}

GameNode *move_edit_stone_node(const GameTree *tree, Point p) {
    const Board *b = &tree->current->board;
    if (!board_contains(b, p) || board_at(b, p) == STONE_EMPTY) return NULL;
    for (GameNode *n = tree->current; n; n = n->parent) {
        if (n->has_move && n->move.point.x == p.x && n->move.point.y == p.y && n->move.color == board_at(b, p)) return n;
    }
    return NULL;
}
