#include "ui/ui.h"

#include "core/markup.h"
#include "ui/clay_render.h"

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

// ---- Markup (TR SQ CR MA LB AR LN TB TW, CataGui shading and pen) -------------------------------------

static Vector2 board_xy(const BoardGeometry *g, f32 x, f32 y) { return (Vector2){ g->origin.x + g->cell * x, g->origin.y + g->cell * y }; }

// Ink that reads on whatever is under the point.
static Color mark_ink(const Theme *t, Stone under) {
    if (under == STONE_BLACK) return t->colors.white_stone;
    if (under == STONE_WHITE) return t->colors.black_stone;
    return color_with_alpha(t->colors.black_stone, 0.9f);
}

static void draw_arrow(const BoardGeometry *g, Point a, Point b, Color c, bool head) {
    Vector2 p = point_center(g, a), q = point_center(g, b);
    f32 dx = q.x - p.x, dy = q.y - p.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    f32 ux = dx / len, uy = dy / len, w = g->cell * 0.08f, hl = g->cell * 0.36f;
    Vector2 end = head ? (Vector2){ q.x - ux * hl * 0.7f, q.y - uy * hl * 0.7f } : q;
    DrawLineEx(p, end, w, c);
    DrawCircleV(p, w * 0.5f, c);
    if (!head) {
        DrawCircleV(q, w * 0.5f, c);
        return;
    }
    Vector2 left = { q.x - ux * hl - uy * hl * 0.45f, q.y - uy * hl + ux * hl * 0.45f };
    Vector2 right = { q.x - ux * hl + uy * hl * 0.45f, q.y - uy * hl - ux * hl * 0.45f };
    DrawTriangle(q, left, right, c);
    DrawTriangle(q, right, left, c);   // either winding
}

static void board_draw_markup(Ui *ui, const BoardGeometry *g, const Markup *m, const Board *board) {
    const Theme *t = ui->theme;
    i32 n = m->width * m->height;
    if (m->width != board->width || m->height != board->height) return;
    // Area shading: translucent cells, over stones too (dead stones are counted with
    // their area), with slowly moving diagonal stripes. Runs of one colour along a row
    // are drawn as one area, and the stripes follow the screen's diagonal, so they run
    // on across neighbouring points and rows without seams and wrap around forever.
    // The stripes slide along their normal and slowly turn (a full turn in ~52 s).
    f32 spacing = g->cell * 0.46f, stripe = spacing * 0.36f;
    f64 now = GetTime();
    f32 angle = 0.785f + (f32)fmod(now * 0.12, 2.0 * PI);
    f32 nx = cosf(angle), ny = sinf(angle), tx = -ny, ty = nx;   // normal, and the stripe direction
    f32 phase = fmodf((f32)(now * 0.3) * g->cell, spacing);
    for (i32 y = 0; y < m->height; ++y) {
        for (i32 x = 0; x < m->width;) {
            i32 i = y * m->width + x;
            if (!m->shaded[i]) {
                ++x;
                continue;
            }
            u32 rgb = m->shade[i];
            i32 end = x;
            while (end + 1 < m->width && m->shaded[i + end + 1 - x] && m->shade[i + end + 1 - x] == rgb) ++end;
            Vector2 a = point_center(g, (Point){ (i8)x, (i8)y });
            Rectangle r = { a.x - g->cell * 0.5f, a.y - g->cell * 0.5f, g->cell * (f32)(end - x + 1), g->cell };
            Color fill = { (u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, 105 };
            DrawRectangleRec(r, fill);
            f32 luma = 0.299f * fill.r + 0.587f * fill.g + 0.114f * fill.b;   // light shades: darker stripes, dark ones: lighter
            Color band = color_lerp(fill, luma > 150.0f ? (Color){ 0, 0, 0, 255 } : (Color){ 255, 255, 255, 255 }, 0.35f);
            band.a = 120;
            render_push_scissor(r);   // within whatever clip the board is drawn in
            // Lines n.p = c, clipped to the area: the range of c and of the position along
            // each line come from projecting the area's corners.
            f32 cmin = INFINITY, cmax = -INFINITY, smin = INFINITY, smax = -INFINITY;
            for (i32 k = 0; k < 4; ++k) {
                f32 px = r.x + (k & 1 ? r.width : 0), py = r.y + (k & 2 ? r.height : 0);
                cmin = fminf(cmin, px * nx + py * ny), cmax = fmaxf(cmax, px * nx + py * ny);
                smin = fminf(smin, px * tx + py * ty), smax = fmaxf(smax, px * tx + py * ty);
            }
            for (f32 c = floorf(cmin / spacing) * spacing - spacing + phase; c < cmax + spacing; c += spacing) {
                Vector2 a0 = { nx * c + tx * (smin - stripe), ny * c + ty * (smin - stripe) };
                Vector2 a1 = { nx * c + tx * (smax + stripe), ny * c + ty * (smax + stripe) };
                DrawLineEx(a0, a1, stripe, band);
            }
            render_pop_scissor();
            x = end + 1;
        }
    }
    // Territory (TB / TW): small squares.
    for (i32 i = 0; i < n; ++i) {
        if (!m->territory[i]) continue;
        Vector2 p = point_center(g, board_point(board, i));
        f32 s = g->cell * 0.36f;
        Color fill = m->territory[i] == STONE_BLACK ? t->colors.black_stone : t->colors.white_stone;
        Color edge = m->territory[i] == STONE_BLACK ? t->colors.black_stone_edge : t->colors.white_stone_edge;
        DrawRectangleV((Vector2){ p.x - s * 0.5f, p.y - s * 0.5f }, (Vector2){ s, s }, fill);
        DrawRectangleLinesEx((Rectangle){ p.x - s * 0.5f, p.y - s * 0.5f, s, s }, 1.0f, edge);
    }
    // Shapes and labels.
    f32 w = fmaxf(1.5f, g->cell * 0.07f);
    for (i32 i = 0; i < n; ++i) {
        Stone under = (Stone)board->cells[i];
        Vector2 p = point_center(g, board_point(board, i));
        Color ink = mark_ink(t, under);
        f32 r = g->cell * 0.26f;
        switch ((MarkShape)m->shape[i]) {
            case MARK_TRIANGLE: {
                Vector2 a = { p.x, p.y - r * 1.05f }, b = { p.x - r * 0.95f, p.y + r * 0.62f }, c = { p.x + r * 0.95f, p.y + r * 0.62f };
                DrawLineEx(a, b, w, ink), DrawLineEx(b, c, w, ink), DrawLineEx(c, a, w, ink);
                break;
            }
            case MARK_SQUARE: DrawRectangleLinesEx((Rectangle){ p.x - r * 0.8f, p.y - r * 0.8f, r * 1.6f, r * 1.6f }, w, ink); break;
            case MARK_CIRCLE: DrawRing(p, r * 0.85f - w * 0.5f, r * 0.85f + w * 0.5f, 0, 360, 40, ink); break;
            case MARK_CROSS:
                DrawLineEx((Vector2){ p.x - r * 0.75f, p.y - r * 0.75f }, (Vector2){ p.x + r * 0.75f, p.y + r * 0.75f }, w, ink);
                DrawLineEx((Vector2){ p.x - r * 0.75f, p.y + r * 0.75f }, (Vector2){ p.x + r * 0.75f, p.y - r * 0.75f }, w, ink);
                break;
            default: break;
        }
        if (m->label[i].text[0]) {
            StringView text = sv(m->label[i].text);
            if (under == STONE_EMPTY) {   // clear the grid under the text
                Color bg = m->shaded[i] ? (Color){ (u8)(m->shade[i] >> 16), (u8)(m->shade[i] >> 8), (u8)m->shade[i], 255 } : t->colors.board;
                DrawCircleV(p, g->cell * 0.36f, m->shaded[i] ? color_lerp(t->colors.board, bg, 0.41f) : bg);
            }
            font_draw_centered(ui->fonts, FONT_UI_BOLD, g->cell * (text.len >= 3 ? 0.36f : text.len == 2 ? 0.44f : 0.52f), text, p, ink);
        }
    }
    // Lines and arrows, then pen strokes, on top.
    Color line_ink = color_with_alpha(t->colors.accent, 0.95f);
    for (i32 i = 0; i < m->line_count; ++i) draw_arrow(g, m->lines[i].from, m->lines[i].to, line_ink, false);
    for (i32 i = 0; i < m->arrow_count; ++i) draw_arrow(g, m->arrows[i].from, m->arrows[i].to, line_ink, true);
    for (i32 s = 0; s < m->stroke_count; ++s) {
        const PenStroke *st = &m->strokes[s];
        Color c = { (u8)(st->rgb >> 16), (u8)(st->rgb >> 8), (u8)st->rgb, 235 };
        f32 pw = fmaxf(2.0f, g->cell * 0.09f);
        for (i32 k = 0; k + 1 < st->count; ++k) {
            Vector2 a = board_xy(g, st->points[k][0], st->points[k][1]), b = board_xy(g, st->points[k + 1][0], st->points[k + 1][1]);
            DrawLineEx(a, b, pw, c);
            DrawCircleV(b, pw * 0.5f, c);
        }
        if (st->count) DrawCircleV(board_xy(g, st->points[0][0], st->points[0][1]), pw * 0.5f, c);
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

    // Playing the engine with the evaluation hidden: no candidates, territory or previews.
    // (Also none while marking up: the marks stay readable.)
    const AnalysisSnapshot *analysis = app->analysis_valid && !app_eval_hidden(app) && !ui->markup_mode ? app->analysis : NULL;
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
    }
    Markup *markup = arena_new(&ui->frame, Markup);
    markup_read(node, board->width, board->height, markup);
    if (!app->config.show_move_numbers && node->has_move && !point_is_pass(node->move.point)) {
        i32 i = board_index(board, node->move.point);
        if (!markup->shape[i] && !markup->label[i].text[0]) {   // a mark on the last move replaces the marker
            DrawCircleV(point_center(&g, node->move.point), g.cell * 0.16f * stone_anim_marker(&ui->stone_anim, i, now), ui->theme->colors.last_move);
        }
    }
    board_draw_markup(ui, &g, markup, board);
    markup_free(markup);
    // Branch moves: a small dot where each following move was played, in its colour;
    // when there are several, the alternatives to the main continuation get a ring.
    if (app->config.show_branches) {
        i32 index = 0;
        for (const GameNode *child = node->first_child; child; child = child->next_sibling, ++index) {
            if (!child->has_move || point_is_pass(child->move.point) || board_at(board, child->move.point) != STONE_EMPTY) continue;
            Vector2 p = point_center(&g, child->move.point);
            bool black = child->move.color == STONE_BLACK;
            f32 rad = g.cell * 0.17f;
            DrawCircleV(p, rad, black ? ui->theme->colors.black_stone : ui->theme->colors.white_stone);
            DrawRing(p, rad - 1.0f, rad, 0, 360, 24, black ? ui->theme->colors.black_stone_edge : color_with_alpha(ui->theme->colors.black_stone, 0.6f));
            if (index > 0) DrawRing(p, rad + 1.5f, rad + 3.0f, 0, 360, 28, ui->theme->colors.accent);
        }
    }
    if (ui->markup_mode) markup_tools_preview(ui, &g);

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
        if (app->config.show_human_policy && !app_eval_hidden(app) && !ui->markup_mode && app_human_policy_for(app, node)) draw_human_policy(ui, &g, app->human_policy, analysis, board);
        // Ghost stone under the cursor: appears and disappears at once (a fade leaves
        // a trail of half-visible stones behind the moving cursor).
        Point hp = ui->hover_point;
        if (hp.x >= 0 && hp.y >= 0 && hp.x < board->width && hp.y < board->height && board_at(board, hp) == STONE_EMPTY &&
            board_check(board, (Move){ node->to_play, hp }, app->tree->rules) == MOVE_OK) {
            draw_stone(ui->theme, point_center(&g, hp), r, node->to_play, 0.45f);
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
    if (ui->markup_mode) {   // the board is a drawing surface: no moves, no hover stone
        ui->board_geo = g;
        markup_tools_input(ui, &g);
        if (ui->wheel > 0) command_execute(&app->commands, SV("nav.back"));
        if (ui->wheel < 0) command_execute(&app->commands, SV("nav.forward"));
        return;
    }
    if (!CheckCollisionPointRec(ui->mouse, g.board)) return;
    ui->board_geo = g;
    Point p = point_at(&g, ui->mouse);
    ui->hover_point = p;

    if (app->analysis_valid && !app_eval_hidden(app) && !point_is_pass(p) && app->config.show_candidates) {
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
