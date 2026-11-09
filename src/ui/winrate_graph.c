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

    // Rank estimate per phase: a dashed divider where each phase starts, and
    // the estimated ranks ("● 3d ○ 1d") at the top of each phase's span.
    if (ui->app->rank_valid) {
        i32 points = game_tree_width(ui->app->tree) * game_tree_height(ui->app->tree);
        f32 size = 11.0f * ui->scale;
        f32 phase_x[RANK_PHASES + 1];
        for (i32 ph = 0; ph < RANK_PHASES; ++ph) {
            i32 start = rank_phase_start(ph, points);
            isize index = st->count;
            for (isize i = 0; i < st->count; ++i) {
                if (st->line[i]->move_number >= start) {
                    index = i;
                    break;
                }
            }
            phase_x[ph] = index >= st->count ? r.x + r.width : r.x + step * (f32)index;
        }
        phase_x[RANK_PHASE_OPENING] = r.x;
        phase_x[RANK_PHASES] = r.x + r.width;
        Color divider = color_with_alpha(c->human_policy, 0.55f);
        for (i32 ph = 0; ph < RANK_PHASES; ++ph) {
            f32 x0 = phase_x[ph], x1 = phase_x[ph + 1];
            if (x1 - x0 < 2.0f) continue;   // the game did not reach this phase
            if (ph > 0) {
                for (f32 y = r.y; y < r.y + r.height; y += 6.0f * ui->scale) {
                    DrawLineEx((Vector2){ x0, y }, (Vector2){ x0, fminf(y + 3.0f * ui->scale, r.y + r.height) }, 1.0f, divider);
                }
            }
            const RankSummary *rs = &ui->app->rank;
            i32 b = rank_tally_best(&rs->phase[ph][STONE_BLACK]), w = rank_tally_best(&rs->phase[ph][STONE_WHITE]);
            if (b < 0 && w < 0) continue;
            char text[48];
            snprintf(text, sizeof(text), "\xE2\x97\x8F %.*s  \xE2\x97\x8B %.*s", (int)rank_label(b).len, rank_label(b).data,
                     (int)rank_label(w).len, rank_label(w).data);   // ● ○
            Vector2 m = font_measure(ui->fonts, FONT_MONO, size, sv(text));
            if (m.x > x1 - x0 - 4.0f * ui->scale) continue;   // no room in a short phase
            font_draw_centered(ui->fonts, FONT_MONO, size, sv(text), (Vector2){ (x0 + x1) * 0.5f, r.y + m.y * 0.5f + 3.0f * ui->scale },
                               c->human_policy);
        }
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
