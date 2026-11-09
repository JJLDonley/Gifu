// Variation tree: every node as a dot, columns = depth, rows = branches.
// The main line of each subtree stays on its parent's row; each additional
// child starts on the next free row (simple, stable, never overlaps).
#include "ui/ui.h"

#include <math.h>

enum { TREE_MAX_NODES = 6000 };

typedef struct TreeItem {
    GameNode *node;
    i32 col, row;
    i32 parent;   // index into items, -1 for root
} TreeItem;

typedef struct TreeView {
    Ui *ui;
    TreeItem *items;
    i32 count;
    i32 cols, rows;
    f32 spacing;
} TreeView;

typedef struct StackEntry {
    GameNode *node;
    i32 col, row, parent;
} StackEntry;

static void build(TreeView *view, GameNode *root) {
    StackEntry *stack = arena_new_array(&view->ui->frame, StackEntry, TREE_MAX_NODES);
    i32 top = 0, next_row = 1;
    stack[top++] = (StackEntry){ root, 0, 0, -1 };
    while (top > 0 && view->count < TREE_MAX_NODES) {
        StackEntry e = stack[--top];
        // Side branches get their row when they are reached (DFS order), so every
        // branch owns a whole row and lines never cross.
        if (e.row < 0) e.row = next_row++;
        i32 index = view->count++;
        view->items[index] = (TreeItem){ e.node, e.col, e.row, e.parent };
        view->cols = MAX(view->cols, e.col + 1);
        view->rows = MAX(view->rows, e.row + 1);
        // Push children in reverse so the first child (main line) is visited first.
        GameNode *kids[64];
        i32 n = 0;
        for (GameNode *c = e.node->first_child; c && n < (i32)countof(kids); c = c->next_sibling) kids[n++] = c;
        for (i32 k = n - 1; k >= 0 && top < TREE_MAX_NODES; --k) {
            stack[top++] = (StackEntry){ kids[k], e.col + 1, k == 0 ? e.row : -1, index };
        }
    }
}

static Vector2 item_pos(const TreeView *v, const TreeItem *it, Rectangle r) {
    return (Vector2){ r.x + v->spacing * (0.5f + (f32)it->col), r.y + v->spacing * (0.5f + (f32)it->row) };
}

static bool on_active_path(const Ui *ui, const GameNode *node) {
    for (const GameNode *n = ui->app->tree->current; n; n = n->parent) {
        if (n == node) return true;
    }
    return false;
}

static void draw_tree(void *user, Rectangle r) {
    TreeView *v = user;
    Ui *ui = v->ui;
    const ThemeColors *c = &ui->theme->colors;
    f32 radius = v->spacing * 0.28f;
    for (i32 i = 0; i < v->count; ++i) {
        const TreeItem *it = &v->items[i];
        if (it->parent < 0) continue;
        Vector2 a = item_pos(v, &v->items[it->parent], r), b = item_pos(v, it, r);
        bool active = on_active_path(ui, it->node);
        Color line = color_with_alpha(c->text_muted, active ? 0.9f : 0.35f);
        if (it->row != v->items[it->parent].row) {
            // Branch: drop down from the parent's column, then step right.
            Vector2 corner = { a.x, b.y };
            DrawLineEx(a, corner, active ? 2.0f : 1.0f, line);
            DrawLineEx(corner, b, active ? 2.0f : 1.0f, line);
        } else {
            DrawLineEx(a, b, active ? 2.0f : 1.0f, line);
        }
    }
    for (i32 i = 0; i < v->count; ++i) {
        const TreeItem *it = &v->items[i];
        Vector2 p = item_pos(v, it, r);
        bool active = on_active_path(ui, it->node);
        f32 alpha = active ? 1.0f : 0.55f;
        if (!it->node->has_move) {
            DrawRectangleRec((Rectangle){ p.x - radius, p.y - radius, radius * 2, radius * 2 }, color_with_alpha(c->text_muted, alpha));
        } else {
            Stone s = it->node->move.color;
            DrawCircleV(p, radius, color_with_alpha(s == STONE_BLACK ? c->black_stone : c->white_stone, alpha));
            DrawRing(p, radius - 1.0f, radius, 0, 360, 24, color_with_alpha(s == STONE_BLACK ? c->black_stone_edge : c->white_stone_edge, alpha));
        }
        if (game_node_find_prop(it->node, SV("C"))) DrawCircleV((Vector2){ p.x + radius, p.y - radius }, radius * 0.35f, c->accent);
        if (it->node == ui->app->tree->current) DrawRing(p, radius + 2.0f, radius + 3.5f, 0, 360, 32, c->accent);
    }
}

void variation_tree_element(Ui *ui, f32 height) {
    TreeView *view = arena_new(&ui->frame, TreeView);
    view->ui = ui;
    view->items = arena_new_array(&ui->frame, TreeItem, TREE_MAX_NODES);
    view->spacing = ui_px(ui, 16);
    build(view, ui->app->tree->root);

    Clay_ElementId clip_id = CLAY_ID("VariationTreeClip");
    Clay_ElementId canvas_id = CLAY_ID("VariationTree");
    f32 width = view->spacing * (f32)view->cols, content_h = view->spacing * (f32)view->rows;
    f32 visible_h = fminf(ui_px(ui, height), content_h + ui_px(ui, 4));
    CLAY({ .id = clip_id,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(visible_h) } },
           .backgroundColor = color_to_clay(color_with_alpha(ui->theme->colors.surface_alt, 0.6f)),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)),
           .clip = { .horizontal = true, .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        CLAY({ .id = canvas_id,
               .layout = { .sizing = { CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(content_h) } },
               .custom = { .customData = ui_custom(ui, draw_tree, view) } }) {}
    }

    // Click a node to jump to it (hit-test against last frame's layout).
    Clay_ElementData canvas = Clay_GetElementData(canvas_id);
    if (canvas.found && ui->mouse_pressed && Clay_PointerOver(clip_id) && !ui->pointer_over_menu) {
        Rectangle r = { canvas.boundingBox.x, canvas.boundingBox.y, canvas.boundingBox.width, canvas.boundingBox.height };
        f32 best = view->spacing * 0.6f;
        for (i32 i = 0; i < view->count; ++i) {
            Vector2 p = item_pos(view, &view->items[i], r);
            f32 d = sqrtf((p.x - ui->mouse.x) * (p.x - ui->mouse.x) + (p.y - ui->mouse.y) * (p.y - ui->mouse.y));
            if (d < best) {
                best = d;
                app_goto(ui->app, view->items[i].node);
            }
        }
    }

    // Keep the current node visible when it changes.
    GameNode *current = ui->app->tree->current;
    Clay_ScrollContainerData scroll = Clay_GetScrollContainerData(clip_id);
    if (scroll.found && scroll.scrollPosition && ui->tree_scrolled_to != current) {
        for (i32 i = 0; i < view->count; ++i) {
            if (view->items[i].node != current) continue;
            f32 x = view->spacing * (0.5f + (f32)view->items[i].col), y = view->spacing * (0.5f + (f32)view->items[i].row);
            f32 vw = scroll.scrollContainerDimensions.width, vh = scroll.scrollContainerDimensions.height;
            Clay_Vector2 *pos = scroll.scrollPosition;   // negative offsets
            if (x + pos->x > vw - view->spacing) pos->x = -(x - vw + view->spacing * 2);
            if (x + pos->x < view->spacing) pos->x = -MAX(0.0f, x - view->spacing * 2);
            if (y + pos->y > vh - view->spacing) pos->y = -(y - vh + view->spacing * 2);
            if (y + pos->y < view->spacing) pos->y = -MAX(0.0f, y - view->spacing * 2);
            break;
        }
        ui->tree_scrolled_to = current;
    }
}
