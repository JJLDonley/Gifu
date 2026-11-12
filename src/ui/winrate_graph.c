#include "ui/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { GRAPH_MAX_NODES = 2048 };

typedef struct GraphState {
    Ui *ui;
    GameNode **line;
    isize count;
} GraphState;

static void draw_graph(void *user, Rectangle r) {
    GraphState *st = user;
    Ui *ui = st->ui;
    const ThemeColors *c = &ui->theme->colors;
    DrawRectangleRec(r, color_with_alpha(c->surface_alt, 0.6f));
    f32 mid = r.y + r.height * 0.5f;
    DrawLineEx((Vector2){ r.x, mid }, (Vector2){ r.x + r.width, mid }, 1.0f, color_with_alpha(c->text_muted, 0.35f));
    if (st->count < 2) return;

    f32 step = r.width / (f32)(st->count - 1);
    f32 max_score = 10.0f;
    for (isize i = 0; i < st->count; ++i) {
        if (st->line[i]->eval.valid) max_score = fmaxf(max_score, fabsf(st->line[i]->eval.black_score));
    }

    // Black win-rate area + line, score line.
    Vector2 prev = { 0 }, prev_score = { 0 };
    bool has_prev = false;
    for (isize i = 0; i < st->count; ++i) {
        const NodeEval *e = &st->line[i]->eval;
        if (!e->valid) {
            has_prev = false;
            continue;
        }
        f32 x = r.x + step * (f32)i;
        Vector2 p = { x, r.y + r.height * (1.0f - e->black_winrate) };
        Vector2 s = { x, mid - (e->black_score / max_score) * r.height * 0.45f };
        if (has_prev) {
            DrawTriangle(prev, (Vector2){ prev.x, r.y + r.height }, (Vector2){ p.x, r.y + r.height }, color_with_alpha(c->winrate_black, 0.12f));
            DrawTriangle(prev, (Vector2){ p.x, r.y + r.height }, p, color_with_alpha(c->winrate_black, 0.12f));
            DrawLineEx(prev_score, s, 1.5f * ui->scale, color_with_alpha(c->score_line, 0.9f));
            DrawLineEx(prev, p, 2.0f * ui->scale, c->graph_line);
        }
        prev = p;
        prev_score = s;
        has_prev = true;
    }

    // Current node marker.
    for (isize i = 0; i < st->count; ++i) {
        if (st->line[i] != ui->app->tree->current) continue;
        f32 x = r.x + step * (f32)i;
        DrawLineEx((Vector2){ x, r.y }, (Vector2){ x, r.y + r.height }, 1.5f, c->accent);
    }
    // Hover marker.
    if (CheckCollisionPointRec(ui->mouse, r)) {
        f32 x = ui->mouse.x;
        DrawLineEx((Vector2){ x, r.y }, (Vector2){ x, r.y + r.height }, 1.0f, color_with_alpha(c->text_muted, 0.6f));
    }
}

void winrate_graph_element(Ui *ui, f32 height) {
    GraphState *st = arena_new(&ui->frame, GraphState);
    st->ui = ui;
    st->line = arena_new_array(&ui->frame, GameNode *, GRAPH_MAX_NODES);
    st->count = MIN(game_tree_active_line(ui->app->tree, st->line, GRAPH_MAX_NODES), GRAPH_MAX_NODES);

    Clay_ElementId id = CLAY_ID("WinrateGraph");
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, height)) } },
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)),
           .custom = { .customData = ui_custom(ui, draw_graph, st) } }) {}

    // Click / drag to navigate.
    Clay_ElementData data = Clay_GetElementData(id);
    if (data.found && st->count > 1 && ui->mouse_down && Clay_PointerOver(id) && ui->active_id == 0) {
        f32 t = (ui->mouse.x - data.boundingBox.x) / fmaxf(1.0f, data.boundingBox.width);
        isize index = (isize)lroundf(CLAMP(t, 0.0f, 1.0f) * (f32)(st->count - 1));
        app_goto(ui->app, st->line[index]);
    }
}
