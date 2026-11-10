#include "ui/ui.h"

#include <math.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>

static const char GTP_COLUMNS[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";

// ---- Geometry ---------------------------------------------------------------------------

static BoardGeometry compute_geometry(Rectangle bounds, i32 width, i32 height, bool coords) {
    BoardGeometry g = { .bounds = bounds, .width = width, .height = height };
    f32 margin_cells = coords ? 1.35f : 0.75f;   // space around the grid (in cells)
    f32 cell_w = bounds.width / ((f32)(width - 1) + 2.0f * margin_cells);
    f32 cell_h = bounds.height / ((f32)(height - 1) + 2.0f * margin_cells);
    g.cell = floorf(fminf(cell_w, cell_h));
    if (g.cell < 4) return g;
    f32 grid_w = g.cell * (f32)(width - 1), grid_h = g.cell * (f32)(height - 1);
    g.origin = (Vector2){ floorf(bounds.x + (bounds.width - grid_w) * 0.5f) + 0.5f,
                          floorf(bounds.y + (bounds.height - grid_h) * 0.5f) + 0.5f };
    f32 pad = g.cell * margin_cells;
    g.board = (Rectangle){ g.origin.x - pad, g.origin.y - pad, grid_w + 2 * pad, grid_h + 2 * pad };
    g.valid = true;
    return g;
}

static Vector2 point_center(const BoardGeometry *g, Point p) {
    return (Vector2){ g->origin.x + g->cell * (f32)p.x, g->origin.y + g->cell * (f32)p.y };
}

// Nearest point, clamped to the board (for drags that leave the grid).
static Point point_at_clamped(const BoardGeometry *g, Vector2 pos) {
    i32 x = (i32)floorf((pos.x - g->origin.x) / g->cell + 0.5f);
    i32 y = (i32)floorf((pos.y - g->origin.y) / g->cell + 0.5f);
    return (Point){ (i8)CLAMP(x, 0, g->width - 1), (i8)CLAMP(y, 0, g->height - 1) };
}

static Point point_at(const BoardGeometry *g, Vector2 pos) {
    if (!g->valid) return POINT_PASS;
    i32 x = (i32)floorf((pos.x - g->origin.x) / g->cell + 0.5f);
    i32 y = (i32)floorf((pos.y - g->origin.y) / g->cell + 0.5f);
    if (x < 0 || y < 0 || x >= g->width || y >= g->height) return POINT_PASS;
    return (Point){ (i8)x, (i8)y };
}

// ---- Stones -------------------------------------------------------------------------------

static void draw_stone(const Theme *t, Vector2 c, f32 r, Stone color, f32 alpha) {
    const ThemeColors *col = &t->colors;
    Color fill = color == STONE_BLACK ? col->black_stone : col->white_stone;
    Color edge = color == STONE_BLACK ? col->black_stone_edge : col->white_stone_edge;
    fill = color_with_alpha(fill, alpha * fill.a / 255.0f);
    edge = color_with_alpha(edge, alpha * edge.a / 255.0f);

    if (t->stones.shadow > 0 && alpha > 0.6f) {
        DrawCircleV((Vector2){ c.x + r * 0.10f, c.y + r * 0.14f }, r * 1.02f, color_with_alpha(BLACK, t->stones.shadow * alpha));
    }
    switch (t->stones.style) {
        case STONE_STYLE_GLOSSY: {
            Color rim = color == STONE_BLACK ? color_lerp(fill, WHITE, 0.08f) : color_lerp(fill, BLACK, 0.16f);
            DrawCircleGradient(c, r, fill, rim);
            Color hi = color == STONE_BLACK ? color_with_alpha(WHITE, 0.22f * alpha) : color_with_alpha(WHITE, 0.85f * alpha);
            DrawCircleGradient((Vector2){ c.x - r * 0.32f, c.y - r * 0.34f }, r * 0.55f, hi, color_with_alpha(WHITE, 0));
            DrawRing(c, r - 1.0f, r, 0, 360, 48, color_with_alpha(edge, 0.55f * alpha));
            break;
        }
        case STONE_STYLE_INK: {
            DrawCircleV(c, r, fill);
            f32 w = color == STONE_WHITE ? fmaxf(1.2f, r * 0.08f) : 1.0f;
            DrawRing(c, r - w, r, 0, 360, 48, edge);
            break;
        }
        case STONE_STYLE_FLAT:
        default:
            DrawCircleV(c, r, fill);
            DrawRing(c, r - fmaxf(1.0f, r * 0.05f), r, 0, 360, 48, edge);
            break;
    }
}

// ---- Board surface -----------------------------------------------------------------------------

static void draw_surface_shadow(Ui *ui, const BoardGeometry *g) {
    const Theme *t = ui->theme;
    if (!t->board.shadow) return;
    f32 radius = ui_px(ui, t->shape.radius * 0.5f);
    f32 roundness = radius * 2.0f / fminf(g->board.width, g->board.height);
    Rectangle s = { g->board.x + 3, g->board.y + 5, g->board.width, g->board.height };
    DrawRectangleRounded(s, roundness, 8, color_with_alpha(BLACK, 0.18f));
}

static void draw_surface(Ui *ui, const BoardGeometry *g) {
    const Theme *t = ui->theme;
    f32 radius = ui_px(ui, t->shape.radius * 0.5f);
    f32 roundness = radius * 2.0f / fminf(g->board.width, g->board.height);
    switch (t->board.style) {
        case BOARD_STYLE_WOOD:
        case BOARD_STYLE_IMAGE:
            if (ui->board_texture.id) {
                Texture2D tex = ui->board_texture;
                DrawTexturePro(tex, (Rectangle){ 0, 0, (f32)tex.width, (f32)tex.height }, g->board, (Vector2){ 0, 0 }, 0, WHITE);
                // Tint towards the theme's board colour so palettes stay coherent.
                DrawRectangleRec(g->board, color_with_alpha(t->colors.board, 0.25f));
                break;
            }
            DrawRectangleRounded(g->board, roundness, 8, t->colors.board);
            break;
        case BOARD_STYLE_PAPER:
            if (ui->background.paper.id) {
                Texture2D tex = ui->background.paper;
                DrawTexturePro(tex, (Rectangle){ 0, 0, (f32)tex.width, (f32)tex.height }, g->board, (Vector2){ 0, 0 }, 0, WHITE);
            } else {
                DrawRectangleRec(g->board, t->colors.board);
            }
            DrawRectangleLinesEx(g->board, 1.0f, color_with_alpha(t->colors.board_line, 0.35f));
            break;
        case BOARD_STYLE_FLAT:
        default:
            DrawRectangleRounded(g->board, roundness, 8, t->colors.board);
            break;
    }
}

static void draw_grid(Ui *ui, const BoardGeometry *g, bool coords) {
    const Theme *t = ui->theme;
    f32 lw = fmaxf(1.0f, t->board.line_width * ui->scale * fminf(1.4f, g->cell / 32.0f));
    Color line = t->colors.board_line;
    for (i32 x = 0; x < g->width; ++x) {
        f32 w = (x == 0 || x == g->width - 1) ? lw * 1.8f : lw;
        Vector2 a = point_center(g, (Point){ (i8)x, 0 }), b = point_center(g, (Point){ (i8)x, (i8)(g->height - 1) });
        DrawLineEx((Vector2){ a.x, a.y - w * 0.5f }, (Vector2){ b.x, b.y + w * 0.5f }, w, line);
    }
    for (i32 y = 0; y < g->height; ++y) {
        f32 w = (y == 0 || y == g->height - 1) ? lw * 1.8f : lw;
        Vector2 a = point_center(g, (Point){ 0, (i8)y }), b = point_center(g, (Point){ (i8)(g->width - 1), (i8)y });
        DrawLineEx((Vector2){ a.x - w * 0.5f, a.y }, (Vector2){ b.x + w * 0.5f, b.y }, w, line);
    }
    if (g->width == g->height) {
        for (i32 y = 0; y < g->height; ++y) {
            for (i32 x = 0; x < g->width; ++x) {
                if (board_is_star_point(g->width, x, y)) DrawCircleV(point_center(g, (Point){ (i8)x, (i8)y }), fmaxf(2.0f, g->cell * 0.1f), line);
            }
        }
    }
    if (!coords) return;
    f32 size = fmaxf(9.0f, g->cell * 0.36f);
    Color label = t->colors.board_label;
    char text[16];
    for (i32 x = 0; x < g->width; ++x) {
        snprintf(text, sizeof(text), "%c", GTP_COLUMNS[x]);
        Vector2 top = point_center(g, (Point){ (i8)x, 0 });
        Vector2 bottom = point_center(g, (Point){ (i8)x, (i8)(g->height - 1) });
        font_draw_centered(ui->fonts, FONT_MONO, size, sv(text), (Vector2){ top.x, top.y - g->cell * 0.85f }, label);
        font_draw_centered(ui->fonts, FONT_MONO, size, sv(text), (Vector2){ bottom.x, bottom.y + g->cell * 0.85f }, label);
    }
    for (i32 y = 0; y < g->height; ++y) {
        snprintf(text, sizeof(text), "%d", g->height - y);
        Vector2 left = point_center(g, (Point){ 0, (i8)y });
        Vector2 right = point_center(g, (Point){ (i8)(g->width - 1), (i8)y });
        font_draw_centered(ui->fonts, FONT_MONO, size, sv(text), (Vector2){ left.x - g->cell * 0.85f, left.y }, label);
        font_draw_centered(ui->fonts, FONT_MONO, size, sv(text), (Vector2){ right.x + g->cell * 0.85f, right.y }, label);
    }
}

// ---- Analysis overlays ---------------------------------------------------------------------------

static void format_visits(char *out, usize n, i32 visits) {
    if (visits >= 10000) snprintf(out, n, "%.0fk", visits / 1000.0);
    else if (visits >= 1000) snprintf(out, n, "%.1fk", visits / 1000.0);
    else snprintf(out, n, "%d", visits);
}

static void draw_ownership(Ui *ui, const BoardGeometry *g, const AnalysisSnapshot *a, const Board *board) {
    const ThemeColors *c = &ui->theme->colors;
    for (i32 y = 0; y < g->height; ++y) {
        for (i32 x = 0; x < g->width; ++x) {
            i32 i = y * g->width + x;
            f32 own = a->ownership[i];
            if (a->to_play == STONE_WHITE) own = -own;   // -> Black's perspective
            f32 mag = fminf(1.0f, fabsf(own));
            if (mag < 0.15f) continue;
            Stone stone = (Stone)board->cells[i];
            Stone owner = own > 0 ? STONE_BLACK : STONE_WHITE;
            if (stone != STONE_EMPTY && stone == owner) continue;   // alive stone: no marker
            f32 s = g->cell * (stone != STONE_EMPTY ? 0.32f : 0.42f) * (0.55f + 0.45f * mag);
            Vector2 p = point_center(g, (Point){ (i8)x, (i8)y });
            Color fill = owner == STONE_BLACK ? c->black_stone : c->white_stone;
            DrawRectangleRec((Rectangle){ p.x - s * 0.5f, p.y - s * 0.5f, s, s }, color_with_alpha(fill, 0.25f + 0.6f * mag));
            if (owner == STONE_WHITE) {
                DrawRectangleLinesEx((Rectangle){ p.x - s * 0.5f, p.y - s * 0.5f, s, s }, 1.0f, color_with_alpha(c->white_stone_edge, 0.5f * mag));
            }
        }
    }
}

static Color candidate_color(const ThemeColors *c, const Candidate *cand, i32 max_visits, bool best) {
    if (best) return c->candidate_best;
    f32 t = max_visits > 0 ? sqrtf((f32)cand->visits / (f32)max_visits) : 0.0f;
    return color_lerp(c->candidate_bad, c->candidate_good, t);
}

static void draw_candidates(Ui *ui, const BoardGeometry *g, const AnalysisSnapshot *a, const Board *board, f32 fade) {
    const ThemeColors *c = &ui->theme->colors;
    i32 max_visits = 0;
    for (i32 i = 0; i < a->candidate_count; ++i) max_visits = MAX(max_visits, a->candidates[i].visits);
    i32 limit = ui->app->config.max_candidates > 0 ? ui->app->config.max_candidates : a->candidate_count;
    for (i32 i = MIN(a->candidate_count, limit) - 1; i >= 0; --i) {
        const Candidate *cand = &a->candidates[i];
        if (point_is_pass(cand->move) || board_at(board, cand->move) != STONE_EMPTY) continue;
        if (max_visits > 0 && cand->visits * 50 < max_visits && i > 2) continue;   // hide noise
        Vector2 p = point_center(g, cand->move);
        f32 r = g->cell * 0.47f;
        Color col = candidate_color(c, cand, max_visits, i == 0);
        DrawCircleV(p, r, color_with_alpha(col, 0.88f * fade));
        if (i == 0) DrawRing(p, r - fmaxf(1.5f, r * 0.10f), r + 0.5f, 0, 360, 48, color_with_alpha(color_lerp(col, BLACK, 0.35f), fade));

        char wr[16], visits[16], score[16];
        snprintf(wr, sizeof(wr), "%.1f", cand->winrate * 100.0f);
        format_visits(visits, sizeof(visits), cand->visits);
        snprintf(score, sizeof(score), "%+.1f", cand->score_lead);
        Color text = color_with_alpha(c->candidate_text, fade);
        if (g->cell >= 34) {
            font_draw_centered(ui->fonts, FONT_UI_BOLD, g->cell * 0.30f, sv(wr), (Vector2){ p.x, p.y - g->cell * 0.17f }, text);
            font_draw_centered(ui->fonts, FONT_UI, g->cell * 0.22f, sv(visits), (Vector2){ p.x, p.y + g->cell * 0.08f }, text);
            font_draw_centered(ui->fonts, FONT_UI, g->cell * 0.20f, sv(score), (Vector2){ p.x, p.y + g->cell * 0.27f }, color_with_alpha(text, 0.75f * fade));
        } else {
            font_draw_centered(ui->fonts, FONT_UI_BOLD, g->cell * 0.34f, sv(wr), p, text);
        }
    }
}

static void draw_pv(Ui *ui, const BoardGeometry *g, const Candidate *cand, Stone to_play, const Board *board) {
    Board b = *board;
    Stone color = to_play;
    char num[8];
    for (i32 i = 0; i < cand->pv_len && i < 30; ++i) {
        Point p = cand->pv[i];
        if (!point_is_pass(p) && board_play(&b, (Move){ color, p }, (BoardRules){ .allow_suicide = true }) == MOVE_OK) {
            Vector2 c = point_center(g, p);
            draw_stone(ui->theme, c, g->cell * 0.48f, color, 0.92f);
            snprintf(num, sizeof(num), "%d", i + 1);
            Color text = color == STONE_BLACK ? ui->theme->colors.white_stone : ui->theme->colors.black_stone;
            font_draw_centered(ui->fonts, FONT_UI_BOLD, g->cell * 0.40f, sv(num), c, text);
        }
        color = stone_opponent(color);
    }
}

static void draw_region(Ui *ui, const BoardGeometry *g, Point a, Point b, bool final);

// ---- Draw callback ----------------------------------------------------------------------------------

// ---- Cached static layer ------------------------------------------------------------------------

static u64 layer_key(Ui *ui, Rectangle bounds, const Board *board) {
    u64 h = 1469598103934665603ull;
    u64 parts[] = { (u64)bounds.width, (u64)bounds.height, (u64)board->width, (u64)board->height,
                    ui->app->config.show_coordinates, (u64)(uintptr_t)ui->theme, (u64)(ui->scale * 1000.0f),
                    ui->board_texture.id, ui->background.paper.id, ui->language_version, ui->theme_editor.version };
    for (i32 i = 0; i < (i32)countof(parts); ++i) h = (h ^ parts[i]) * 1099511628211ull;
    return h;
}

static void rebuild_layer(Ui *ui, Rectangle bounds, const Board *board) {
    i32 w = (i32)bounds.width, h = (i32)bounds.height;
    if (ui->board_layer.id && (ui->board_layer.texture.width != w || ui->board_layer.texture.height != h)) {
        UnloadRenderTexture(ui->board_layer);
        ui->board_layer = (RenderTexture2D){ 0 };
    }
    if (!ui->board_layer.id) {
        ui->board_layer = LoadRenderTexture(w, h);
        SetTextureFilter(ui->board_layer.texture, TEXTURE_FILTER_BILINEAR);
    }
    // Geometry in layer-local coordinates.
    BoardGeometry g = compute_geometry((Rectangle){ 0, 0, bounds.width, bounds.height }, board->width, board->height,
                                       ui->app->config.show_coordinates);
    BeginTextureMode(ui->board_layer);
    ClearBackground(BLANK);
    // Colour: normal "over"; alpha: accumulate (a + d*(1-a)). The texture then
    // holds premultiplied colour with correct coverage, so translucent glyph
    // edges and tints don't punch holes when drawn with BLEND_ALPHA_PREMULTIPLY.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    if (g.valid) {
        draw_surface(ui, &g);
        draw_grid(ui, &g, ui->app->config.show_coordinates);
    }
    EndBlendMode();
    EndTextureMode();
}

void board_view_prepare(Ui *ui) {
    const Board *board = &ui->app->tree->current->board;
    stone_anim_update(&ui->stone_anim, board, GetTime());
    Clay_ElementData data = Clay_GetElementData(CLAY_ID("Board"));
    if (!data.found) return;
    Rectangle bounds = { roundf(data.boundingBox.x), roundf(data.boundingBox.y), roundf(data.boundingBox.width),
                         roundf(data.boundingBox.height) };
    if (bounds.width < 8 || bounds.height < 8) return;
    u64 key = layer_key(ui, bounds, board);
    if (key != ui->board_layer_key || !ui->board_layer.id) {
        rebuild_layer(ui, bounds, board);
        ui->board_layer_key = key;
    }
}

void board_view_free(Ui *ui) {
    if (ui->board_layer.id) UnloadRenderTexture(ui->board_layer);
    ui->board_layer = (RenderTexture2D){ 0 };
}

// ---- Draw callback ----------------------------------------------------------------------------------

// Human SL network moves: rings sized by probability (relative to the most
// likely move), with the percentage when there is room and no candidate on top.
static void draw_human_policy(Ui *ui, const BoardGeometry *g, const HumanPolicy *h, const AnalysisSnapshot *a, const Board *board) {
    const ThemeColors *c = &ui->theme->colors;
    if (h->width != g->width || h->height != g->height) return;
    f32 best = 0;
    for (i32 i = 0; i < h->width * h->height; ++i) best = fmaxf(best, h->policy[i]);
    if (best <= 0) return;
    bool covered[BOARD_MAX_POINTS] = { 0 };   // points showing a KataGo candidate
    if (a && ui->app->config.show_candidates) {
        i32 limit = ui->app->config.max_candidates > 0 ? MIN(ui->app->config.max_candidates, a->candidate_count) : a->candidate_count;
        for (i32 i = 0; i < limit; ++i) {
            if (!point_is_pass(a->candidates[i].move)) covered[board_index(board, a->candidates[i].move)] = true;
        }
    }
    for (i32 y = 0; y < g->height; ++y) {
        for (i32 x = 0; x < g->width; ++x) {
            i32 i = y * g->width + x;
            f32 p = h->policy[i];
            if (p < 0.02f || p < best * 0.08f || board->cells[i] != STONE_EMPTY) continue;
            f32 rel = sqrtf(p / best);
            Vector2 center = point_center(g, (Point){ (i8)x, (i8)y });
            f32 r = g->cell * 0.5f;
            f32 width = fmaxf(1.5f, g->cell * (0.05f + 0.07f * rel));
            DrawRing(center, r - width, r, 0, 360, 48, color_with_alpha(c->human_policy, 0.35f + 0.6f * rel));
            if (!covered[i] && g->cell >= 22) {
                char text[8];
                snprintf(text, sizeof(text), "%.0f", p * 100.0f);
                font_draw_centered(ui->fonts, FONT_UI_BOLD, g->cell * 0.32f, sv(text), center, color_with_alpha(c->human_policy, 0.6f + 0.4f * rel));
            }
        }
    }
}

static void draw_board(void *user, Rectangle bounds) {
    Ui *ui = user;
    App *app = ui->app;
    const GameNode *node = app->tree->current;
    const Board *board = &node->board;
    BoardGeometry g = compute_geometry(bounds, board->width, board->height, app->config.show_coordinates);
    if (!g.valid) return;
    ui->board_geo = g;
    f64 now = GetTime();

    draw_surface_shadow(ui, &g);
    if (ui->board_layer.id && ui->board_layer.texture.width == (i32)bounds.width && ui->board_layer.texture.height == (i32)bounds.height) {
        Texture2D t = ui->board_layer.texture;
        BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
        DrawTextureRec(t, (Rectangle){ 0, 0, (f32)t.width, -(f32)t.height }, (Vector2){ bounds.x, bounds.y }, WHITE);
        EndBlendMode();
    } else {
        draw_surface(ui, &g);   // first frame / resize: draw directly
        draw_grid(ui, &g, app->config.show_coordinates);
    }

    const AnalysisSnapshot *analysis = app->analysis_valid ? app->analysis : NULL;
    if (analysis && app->config.show_ownership && analysis->has_ownership) draw_ownership(ui, &g, analysis, board);

    // Stones: newly placed ones ease in (slight overshoot), removed ones fade out.
    f32 r = g.cell * 0.48f;
    const StoneAnim *sa = &ui->stone_anim;
    for (i32 i = 0; i < sa->ghost_count; ++i) {
        const StoneGhost *gh = &sa->ghosts[i];
        if (gh->point.x >= board->width || gh->point.y >= board->height) continue;
        if (board_at(board, gh->point) != STONE_EMPTY) continue;
        StoneLook look = stone_anim_ghost(gh, now);
        draw_stone(ui->theme, point_center(&g, gh->point), r * look.scale, gh->color, look.alpha);
    }
    for (i32 i = 0; i < board->width * board->height; ++i) {
        if (board->cells[i] == STONE_EMPTY) continue;
        StoneLook look = stone_anim_placed(sa, i, now);
        draw_stone(ui->theme, point_center(&g, board_point(board, i)), r * look.scale, (Stone)board->cells[i], look.alpha);
    }
    // Move numbers (stones still on the board, latest number wins), else last-move marker.
    if (app->config.show_move_numbers) {
        i32 *numbers = arena_new_array(&ui->frame, i32, BOARD_MAX_POINTS);
        for (const GameNode *n = node; n; n = n->parent) {
            if (!n->has_move || point_is_pass(n->move.point)) continue;
            i32 i = board_index(board, n->move.point);
            if (numbers[i] == 0 && board->cells[i] == n->move.color) numbers[i] = n->move_number;
        }
        char text[16];
        for (i32 i = 0; i < board->width * board->height; ++i) {
            if (!numbers[i]) continue;
            snprintf(text, sizeof(text), "%d", numbers[i]);
            Color fg = board->cells[i] == STONE_BLACK ? ui->theme->colors.white_stone : ui->theme->colors.black_stone;
            if (node->has_move && board_index(board, node->move.point) == i) fg = ui->theme->colors.last_move;
            font_draw_centered(ui->fonts, FONT_UI_BOLD, g.cell * (numbers[i] >= 100 ? 0.34f : 0.42f), sv(text),
                               point_center(&g, board_point(board, i)), fg);
        }
    } else if (node->has_move && !point_is_pass(node->move.point)) {
        i32 i = board_index(board, node->move.point);
        DrawCircleV(point_center(&g, node->move.point), g.cell * 0.16f * stone_anim_marker(&ui->stone_anim, i, now), ui->theme->colors.last_move);
    }

    // Candidate preview: hovered on the board or in the candidates panel.
    i32 pv_index = ui->hover_candidate >= 0 ? ui->hover_candidate : ui->hover_list_candidate;
    if (analysis && pv_index >= 0 && pv_index < analysis->candidate_count) {
        draw_pv(ui, &g, &analysis->candidates[pv_index], analysis->to_play, board);
    } else {
        // Candidates fade in whenever a new position gets its first analysis.
        enum { CANDIDATE_KEY = 0xCA4D1D47u };
        if (app->main.synced_position_id != ui->candidates_position) {
            ui->candidates_position = app->main.synced_position_id;
            anim_set(&ui->anim, CANDIDATE_KEY, 0.0f);
        }
        f32 fade = anim_towards(&ui->anim, CANDIDATE_KEY, analysis ? 1.0f : 0.0f, 10.0f, 0.0f);
        if (analysis && app->config.show_candidates && fade > 0.01f) draw_candidates(ui, &g, analysis, board, fade);
        if (app->config.show_human_policy && app_human_policy_for(app, node)) draw_human_policy(ui, &g, app->human_policy, analysis, board);
        // Ghost stone under the cursor, fading in/out per point.
        for (i32 i = 0; i < board->width * board->height; ++i) {
            Point p = board_point(board, i);
            bool hovered = point_eq(p, ui->hover_point) && board->cells[i] == STONE_EMPTY &&
                           board_check(board, (Move){ node->to_play, p }, app->tree->rules) == MOVE_OK;
            f32 a = anim_towards(&ui->anim, 0x60000000u + (u32)i, hovered ? 0.45f : 0.0f, 18.0f, 0.0f);
            if (a > 0.01f && board->cells[i] == STONE_EMPTY) draw_stone(ui->theme, point_center(&g, p), r, node->to_play, a);
        }
    }
    if (ui->region_dragging) draw_region(ui, &g, ui->region_start, ui->region_end, false);
    else if (app->region.active) {
        draw_region(ui, &g, (Point){ app->region.x0, app->region.y0 }, (Point){ app->region.x1, app->region.y1 }, true);
    }
}

// Washes out everything outside the region and outlines it.
static void draw_region(Ui *ui, const BoardGeometry *g, Point a, Point b, bool final) {
    const ThemeColors *c = &ui->theme->colors;
    i32 x0 = MIN(a.x, b.x), x1 = MAX(a.x, b.x), y0 = MIN(a.y, b.y), y1 = MAX(a.y, b.y);
    Rectangle r = { g->origin.x + g->cell * ((f32)x0 - 0.5f), g->origin.y + g->cell * ((f32)y0 - 0.5f),
                    g->cell * (f32)(x1 - x0 + 1), g->cell * (f32)(y1 - y0 + 1) };
    Rectangle bd = g->board;
    Color wash = color_with_alpha(c->surface, final ? 0.55f : 0.35f);
    DrawRectangleRec((Rectangle){ bd.x, bd.y, bd.width, r.y - bd.y }, wash);                                      // above
    DrawRectangleRec((Rectangle){ bd.x, r.y + r.height, bd.width, bd.y + bd.height - r.y - r.height }, wash);     // below
    DrawRectangleRec((Rectangle){ bd.x, r.y, r.x - bd.x, r.height }, wash);                                       // left
    DrawRectangleRec((Rectangle){ r.x + r.width, r.y, bd.x + bd.width - r.x - r.width, r.height }, wash);         // right
    DrawRectangleLinesEx(r, final ? 2.0f : 1.5f, c->accent);
}

void board_view_element(Ui *ui) {
    CLAY({ .id = CLAY_ID("Board"),
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
           .custom = { .customData = ui_custom(ui, draw_board, ui) } }) {}
}

// ---- Input --------------------------------------------------------------------------------------------

void board_view_handle_input(Ui *ui) {
    App *app = ui->app;
    Clay_ElementData data = Clay_GetElementData(CLAY_ID("Board"));
    ui->hover_point = POINT_PASS;
    ui->hover_candidate = -1;
    if (!data.found || ui->pointer_over_menu || ui->dialog != UI_DIALOG_NONE) return;
    const Board *board = &app->tree->current->board;
    Rectangle bounds = { data.boundingBox.x, data.boundingBox.y, data.boundingBox.width, data.boundingBox.height };
    BoardGeometry g = compute_geometry(bounds, board->width, board->height, app->config.show_coordinates);
    if (!g.valid) return;
    // Region selection: drag a rectangle (may leave the board; clamped).
    if (ui->region_select) {
        if (ui->mouse_pressed && CheckCollisionPointRec(ui->mouse, g.board)) {
            ui->region_dragging = true;
            ui->region_start = ui->region_end = point_at_clamped(&g, ui->mouse);
        }
        if (ui->region_dragging) {
            ui->region_end = point_at_clamped(&g, ui->mouse);
            if (ui->mouse_released) {
                ui->region_dragging = ui->region_select = false;
                app_set_region(app, (BoardRegion){ true, ui->region_start.x, ui->region_start.y, ui->region_end.x, ui->region_end.y });
            }
        }
        SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
        return;
    }
    if (!CheckCollisionPointRec(ui->mouse, g.board)) return;
    ui->board_geo = g;
    Point p = point_at(&g, ui->mouse);
    ui->hover_point = p;

    if (app->analysis_valid && !point_is_pass(p) && app->config.show_candidates) {
        for (i32 i = 0; i < app->analysis->candidate_count && i < app->config.max_candidates; ++i) {
            if (point_eq(app->analysis->candidates[i].move, p)) ui->hover_candidate = i;
        }
    }
    if (ui->wheel > 0) command_execute(&app->commands, SV("nav.back"));
    if (ui->wheel < 0) command_execute(&app->commands, SV("nav.forward"));
    if (ui->mouse_pressed && !point_is_pass(p)) {
        MoveError err = app_play(app, p);
        if (err != MOVE_OK && err != MOVE_ERR_OCCUPIED) app_set_status(app, "Illegal move: %s", move_error_name(err));
        ui->hover_candidate = -1;
    }
}
