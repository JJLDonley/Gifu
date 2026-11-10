// Deep report (Hawk Eye): a popup with everything the analysed line says about
// both players — summary, per-phase table, points lost per move, accuracy
// trend, loss distribution and the biggest mistakes. Opened with Y or from the
// sidebar Report; clicking a move or a graph jumps there.
#include "ui/ui.h"

#include "core/game_report.h"

#include <math.h>
#include <stdio.h>

enum { DEEP_MAX_LINE = 4096, TREND_WINDOW = 10 };

typedef f32 RankProbs[RANK_COUNT];

typedef struct DeepReport {
    Ui *ui;
    GameNode **line;
    isize count;
    GameReport report;
    f32 page_h;                  // pixels available to a tab page
    // Human-network rank data per line index (CGR[]), see rank_page.
    f32 (*probs)[RANK_COUNT];
    bool *has_probs;
    i32 prob_moves;
} DeepReport;

static const char *const PHASE_KEYS[GAME_PHASES] = { "report.phase_opening", "report.phase_middle", "report.phase_endgame" };
static const char *const PHASE_FALLBACK[GAME_PHASES] = { "Opening", "Middle", "Endgame" };

// Mid-size losses (2-5 points): amber, nudged towards the theme's "bad" colour.
static Color mid_loss_color(const ThemeColors *c) { return color_lerp((Color){ 232, 160, 40, 255 }, c->candidate_bad, 0.2f); }

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

// Translated text with "{rank}" replaced by a rank label ("9d").
static StringView tr_rank(Ui *ui, const char *key, const char *fallback, StringView rank) {
    StringView text = tr(ui, key, fallback);
    isize at = sv_find(text, SV("{rank}"));
    if (at < 0) return text;
    return arena_format(&ui->frame, SV_FMT SV_FMT SV_FMT, SV_ARG(sv_take(text, at)), SV_ARG(rank), SV_ARG(sv_drop(text, at + 6)));
}

static StringView player_name(Ui *ui, Stone color) {
    StringView name = game_tree_info(ui->app->tree, color == STONE_BLACK ? SV("PB") : SV("PW"));
    if (name.len) return name;
    return color == STONE_BLACK ? tr(ui, "label.black", "Black") : tr(ui, "label.white", "White");
}

static void section_title(Ui *ui, StringView title) {
    CLAY_TEXT(ui_str(ui, title), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 13),
                                                    .textColor = color_to_clay(ui->theme->colors.text_muted), .letterSpacing = 1 }));
}

static void stat_cell(Ui *ui, StringView label, StringView value, Color color) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        ui_label(ui, label, FONT_UI, 12, c->text_muted);
        ui_label(ui, value, FONT_UI_BOLD, 18, color);
    }
}

// ---- Summary ---------------------------------------------------------------------------------------

static void summary_column(DeepReport *d, Stone color) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    const PlayerReport *p = &d->report.player[color];
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 8), .padding = CLAY_PADDING_ALL(ui_u16(ui, 12)) },
           .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            ui_stone_dot(ui, color);
            ui_label(ui, player_name(ui, color), FONT_UI_BOLD, 15, c->text);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
            i32 rank = ui->app->rank_valid ? rank_tally_best(&ui->app->rank.total[color]) : -1;
            if (rank >= 0) {
                CLAY({ .layout = { .padding = { ui_u16(ui, 6), ui_u16(ui, 6), ui_u16(ui, 2), ui_u16(ui, 2) } },
                       .backgroundColor = color_to_clay(color_with_alpha(c->human_policy, 0.15f)),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                    CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_label(rank))), ui_text_style(ui, FONT_UI_BOLD, 13, c->human_policy));
                }
            }
        }
        // Accuracy, large.
        CLAY({ .layout = { .childGap = ui_u16(ui, 8), .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
            CLAY_TEXT(p->moves ? ui_fmt(ui, "%.1f", p->accuracy) : ui_str(ui, SV("—")), ui_text_style(ui, FONT_UI_BOLD, 34, c->text));
            CLAY({ .layout = { .padding = { 0, 0, 0, ui_u16(ui, 7) } } }) {
                ui_label(ui, tr(ui, "report.accuracy_score", "accuracy"), FONT_UI, 13, c->text_muted);
            }
            CLAY({ .layout = { .padding = { 0, 0, 0, ui_u16(ui, 7) } } }) {
                CLAY_TEXT(ui_fmt(ui, "· %d %s", p->moves, p->moves == 1 ? "move" : "moves"), ui_text_style(ui, FONT_UI, 12, c->text_muted));
            }
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10) } }) {
            stat_cell(ui, tr(ui, "report.accuracy", "Best move"), arena_format(&ui->frame, "%.0f%%", player_accuracy(p) * 100.0f), c->text);
            stat_cell(ui, tr(ui, "report.top3", "Top 3"),
                 arena_format(&ui->frame, "%.0f%%", p->moves ? 100.0f * (f32)p->top3 / (f32)p->moves : 0.0f), c->text);
            stat_cell(ui, tr(ui, "report.avg_loss", "Avg loss"), arena_format(&ui->frame, "%.1f%%", p->avg_winrate_loss * 100.0f), c->text);
            stat_cell(ui, tr(ui, "report.avg_score_loss", "Avg points lost"), arena_format(&ui->frame, "%.2f", p->avg_score_loss), c->text);
            stat_cell(ui, tr(ui, "report.agreement", "Engine agreement"),
                      p->agreement_moves ? arena_format(&ui->frame, "%.0f%%", p->agreement) : SV("—"), c->text);
            stat_cell(ui, tr(ui, "report.errors", "Mistakes/Blunders"), arena_format(&ui->frame, "%d / %d", p->mistakes, p->blunders),
                 p->blunders ? c->candidate_bad : c->text);
        }
    }
}

// ---- Phase table --------------------------------------------------------------------------------------

static f32 g_cell_size = 15.0f;   // font size of table cells (set per table from the space it gets)

static void cell(Ui *ui, StringView text, FontRole role, Color color, f32 width, bool right) {
    CLAY({ .layout = { .sizing = { width > 0 ? CLAY_SIZING_FIXED(ui_px(ui, width)) : CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                       .childAlignment = { .x = right ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT } } }) {
        ui_label(ui, text, role, g_cell_size, color);
    }
}

// Logical font size that fits a row of `row_px` pixels.
static f32 font_for_row(Ui *ui, f32 row_px, f32 lo, f32 hi) { return CLAMP(row_px / ui->scale * 0.42f, lo, hi); }

static void phase_table(DeepReport *d) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    g_cell_size = 16.0f;
    // Grows to the rest of the page; the three phase rows share it.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .padding = { 0, 0, 0, ui_u16(ui, 6) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
               .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
            g_cell_size = 13.0f;
            cell(ui, SV(""), FONT_UI, c->text_muted, 0, false);
            for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 22)), CLAY_SIZING_FIT(0) } } }) { ui_stone_dot(ui, color); }
                cell(ui, tr(ui, "report.accuracy_score", "accuracy"), FONT_UI, c->text_muted, 74, true);
                cell(ui, tr(ui, "report.accuracy", "Best move"), FONT_UI, c->text_muted, 74, true);
                cell(ui, tr(ui, "report.avg_score_loss", "Avg points lost"), FONT_UI, c->text_muted, 104, true);
                cell(ui, tr(ui, "report.errors_short", "Mist./Blund."), FONT_UI, c->text_muted, 84, true);
            }
            g_cell_size = 17.0f;
        }
        for (i32 ph = 0; ph < GAME_PHASES; ++ph) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = ui_u16(ui, 8),
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .border = { .color = color_to_clay(color_with_alpha(c->border, 0.5f)), .width = { .bottom = ph < GAME_PHASES - 1 } } }) {
                cell(ui, tr(ui, PHASE_KEYS[ph], PHASE_FALLBACK[ph]), FONT_UI_BOLD, c->text, 0, false);
                for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                    const StageReport *st = &d->report.player[color].stage[ph];
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 22)), CLAY_SIZING_FIT(0) } } }) {}
                    if (st->moves == 0) {
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 74, true);
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 74, true);
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 104, true);
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 84, true);
                        continue;
                    }
                    cell(ui, arena_format(&ui->frame, "%.1f", st->accuracy), FONT_MONO, c->text, 74, true);
                    cell(ui, arena_format(&ui->frame, "%.0f%%", 100.0f * (f32)st->top1 / (f32)st->moves), FONT_MONO, c->text, 74, true);
                    cell(ui, arena_format(&ui->frame, "%.2f", st->avg_score_loss), FONT_MONO,
                         st->avg_score_loss > 2.0f ? c->candidate_bad : c->text, 104, true);
                    cell(ui, arena_format(&ui->frame, "%d / %d", st->mistakes, st->blunders), FONT_MONO,
                         st->blunders ? c->candidate_bad : c->text, 84, true);
                }
            }
        }
    }
    g_cell_size = 15.0f;
}

// ---- Graphs ------------------------------------------------------------------------------------------

typedef struct GraphData {
    DeepReport *d;
    bool trend;   // accuracy trend instead of points lost
} GraphData;

static void phase_dividers(DeepReport *d, Rectangle r, f32 step) {
    Ui *ui = d->ui;
    i32 points = game_tree_width(ui->app->tree) * game_tree_height(ui->app->tree);
    for (i32 ph = 1; ph < GAME_PHASES; ++ph) {
        i32 start = game_phase_start(ph, points);
        for (isize i = 0; i < d->count; ++i) {
            if (d->line[i]->move_number < start) continue;
            f32 x = r.x + step * (f32)i;
            for (f32 y = r.y; y < r.y + r.height; y += 6.0f * ui->scale) {
                DrawLineEx((Vector2){ x, y }, (Vector2){ x, fminf(y + 3.0f * ui->scale, r.y + r.height) }, 1.0f,
                           color_with_alpha(ui->theme->colors.text_muted, 0.4f));
            }
            break;
        }
    }
}

static void draw_loss_graph(void *user, Rectangle r) {
    GraphData *g = user;
    DeepReport *d = g->d;
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    DrawRectangleRec(r, color_with_alpha(c->surface_alt, 0.6f));
    if (d->count < 2) return;
    f32 step = r.width / (f32)(d->count - 1);
    phase_dividers(d, r, step);
    if (!g->trend) {
        // Points lost per move: Black's bars up from the middle, White's down. 15+ points is full height.
        f32 mid = r.y + r.height * 0.5f, half = r.height * 0.5f - 2.0f;
        DrawLineEx((Vector2){ r.x, mid }, (Vector2){ r.x + r.width, mid }, 1.0f, color_with_alpha(c->text_muted, 0.4f));
        for (f32 pts = 5.0f; pts <= 10.0f; pts += 5.0f) {   // 5 and 10 point guides
            f32 off = half * pts / 15.0f;
            DrawLineEx((Vector2){ r.x, mid - off }, (Vector2){ r.x + r.width, mid - off }, 1.0f, color_with_alpha(c->text_muted, 0.15f));
            DrawLineEx((Vector2){ r.x, mid + off }, (Vector2){ r.x + r.width, mid + off }, 1.0f, color_with_alpha(c->text_muted, 0.15f));
        }
        f32 bar = fmaxf(1.0f, fminf(step * 0.7f, 8.0f * ui->scale));
        for (isize i = 0; i < d->count; ++i) {
            f32 loss = 0;
            if (!game_node_score_loss(d->line[i], &loss) || loss <= 0.05f) continue;
            f32 h = half * fminf(loss, 15.0f) / 15.0f;
            f32 x = r.x + step * (f32)i - bar * 0.5f;
            Color col = loss >= 5.0f ? c->candidate_bad : loss >= 2.0f ? mid_loss_color(c) : c->text_muted;
            bool black = d->line[i]->move.color == STONE_BLACK;
            DrawRectangleRec((Rectangle){ x, black ? mid - h : mid, bar, h }, color_with_alpha(col, 0.85f));
        }
    } else {
        // Rolling accuracy (last TREND_WINDOW own moves) for each player. The vertical
        // range is fitted to the data (lowest value rounded down to 10) so it fills the graph.
        f32 *values = arena_new_array(&ui->frame, f32, d->count * 2 + 2);
        f32 lo = 100.0f;
        for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
            f32 window[TREND_WINDOW];
            i32 n = 0, head = 0;
            for (isize i = 0; i < d->count; ++i) {
                f32 *v = &values[(color - 1) * d->count + i];
                *v = -1.0f;
                const GameNode *node = d->line[i];
                f32 loss = 0;
                if (!node->has_move || node->move.color != color || !game_node_score_loss(node, &loss)) continue;
                window[head] = move_accuracy(loss);
                head = (head + 1) % TREND_WINDOW;
                if (n < TREND_WINDOW) n++;
                f32 sum = 0;
                for (i32 k = 0; k < n; ++k) sum += window[k];
                *v = sum / (f32)n;
                lo = fminf(lo, *v);
            }
        }
        lo = fmaxf(0.0f, floorf((lo - 2.0f) / 10.0f) * 10.0f);
        if (lo > 90.0f) lo = 90.0f;
        f32 range = 100.0f - lo;
        f32 label_size = 11.0f * ui->scale;
        for (f32 v = lo; v <= 100.0f; v += 10.0f) {
            f32 y = r.y + r.height * (1.0f - (v - lo) / range);
            DrawLineEx((Vector2){ r.x, y }, (Vector2){ r.x + r.width, y }, 1.0f, color_with_alpha(c->text_muted, 0.15f));
            char label[8];
            snprintf(label, sizeof(label), "%.0f", v);
            font_draw(ui->fonts, FONT_MONO, label_size, sv(label), (Vector2){ r.x + 4.0f * ui->scale, fminf(y + 2.0f, r.y + r.height - label_size) },
                      color_with_alpha(c->text_muted, 0.8f));
        }
        for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
            Vector2 prev = { 0 };
            bool has_prev = false;
            Color col = color == STONE_BLACK ? c->winrate_black : c->score_line;
            for (isize i = 0; i < d->count; ++i) {
                f32 v = values[(color - 1) * d->count + i];
                if (v < 0) continue;
                Vector2 p = { r.x + step * (f32)i, r.y + r.height * (1.0f - (v - lo) / range) };
                if (has_prev) DrawLineEx(prev, p, 2.0f * ui->scale, col);
                prev = p;
                has_prev = true;
            }
        }
    }
    // Current move.
    for (isize i = 0; i < d->count; ++i) {
        if (d->line[i] != ui->app->tree->current) continue;
        f32 x = r.x + step * (f32)i;
        DrawLineEx((Vector2){ x, r.y }, (Vector2){ x, r.y + r.height }, 1.5f, c->accent);
    }
}

static void graph(DeepReport *d, bool trend, u32 index, f32 height) {
    Ui *ui = d->ui;
    GraphData *g = arena_new(&ui->frame, GraphData);
    *g = (GraphData){ d, trend };
    Clay_ElementId id = CLAY_IDI("DeepGraph", index);
    CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), height > 0 ? CLAY_SIZING_FIXED(ui_px(ui, height)) : CLAY_SIZING_GROW(0) } },
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)), .custom = { .customData = ui_custom(ui, draw_loss_graph, g) } }) {}
    Clay_ElementData data = Clay_GetElementData(id);
    if (data.found && d->count > 1 && ui->mouse_down && Clay_PointerOver(id)) {
        f32 t = (ui->mouse.x - data.boundingBox.x) / fmaxf(1.0f, data.boundingBox.width);
        app_goto(ui->app, d->line[(isize)lroundf(CLAMP(t, 0.0f, 1.0f) * (f32)(d->count - 1))]);
    }
}

static void legend_dot(Ui *ui, Color color, StringView text) {
    CLAY({ .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 12)), CLAY_SIZING_FIXED(ui_px(ui, 12)) } },
               .backgroundColor = color_to_clay(color), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
        ui_label(ui, text, FONT_UI, 13, ui->theme->colors.text_muted);
    }
}

// ---- Distribution ------------------------------------------------------------------------------------

static void distribution(DeepReport *d, bool winrate, f32 row_px) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    static const char *const SCORE_LABELS[LOSS_BUCKETS] = { "< 0.5", "0.5–2", "2–5", "5–10", "≥ 10" };
    static const char *const WR_LABELS[LOSS_BUCKETS] = { "< 2%", "2–5%", "5–10%", "10–20%", "≥ 20%" };
    const char *const *LABELS = winrate ? WR_LABELS : SCORE_LABELS;
    f32 font = font_for_row(ui, row_px, 12.0f, 18.0f);
    f32 bar_px = row_px * 0.62f;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        for (i32 b = 0; b < LOSS_BUCKETS; ++b) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_px) }, .childGap = ui_u16(ui, 12),
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, font * 6.0f)), CLAY_SIZING_FIT(0) } } }) {
                    ui_label(ui, sv(LABELS[b]), FONT_MONO, font, c->text);
                }
                for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                    const PlayerReport *p = &d->report.player[color];
                    const i32 *hist = winrate ? p->winrate_hist : p->score_hist;
                    f32 share = p->moves ? (f32)hist[b] / (f32)p->moves : 0.0f;
                    Color col = b >= 3 ? c->candidate_bad : b == 2 ? mid_loss_color(c) : (color == STONE_BLACK ? c->winrate_black : c->score_line);
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_px) }, .childGap = ui_u16(ui, 8),
                                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(fmaxf(share * 0.8f, 0.004f)), CLAY_SIZING_GROW(0) } },
                               .backgroundColor = color_to_clay(color_with_alpha(col, 0.8f)),
                               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
                        CLAY_TEXT(ui_fmt(ui, "%d · %.0f%%", hist[b], share * 100.0f), ui_text_style(ui, FONT_MONO, font * 0.85f, c->text_muted));
                    }
                }
            }
        }
    }
}

// ---- Biggest mistakes -----------------------------------------------------------------------------------

static void mistakes(DeepReport *d) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    // Merge both players' worst lists by win-rate loss.
    GameNode *rows[REPORT_WORST * 2];
    f32 loss[REPORT_WORST * 2];
    i32 n = 0;
    for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
        const PlayerReport *p = &d->report.player[color];
        for (i32 i = 0; i < p->worst_count; ++i) {
            i32 at = n++;
            while (at > 0 && loss[at - 1] < p->worst_loss[i]) {
                rows[at] = rows[at - 1];
                loss[at] = loss[at - 1];
                --at;
            }
            rows[at] = p->worst[i];
            loss[at] = p->worst_loss[i];
        }
    }
    n = MIN(n, REPORT_WORST);
    if (n == 0) {
        ui_label(ui, tr(ui, "report.no_mistakes", "No analysed moves yet: run Analyze game."), FONT_UI, 15, c->text_muted);
        return;
    }
    f32 row_px = (d->page_h - ui_px(ui, 72)) / (f32)REPORT_WORST;
    g_cell_size = font_for_row(ui, row_px, 13.0f, 17.0f);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {   // no gap between rows
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                       .padding = { ui_u16(ui, 6), ui_u16(ui, 6), 0, ui_u16(ui, 4) } },
           .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
        cell(ui, tr(ui, "label.move", "Move"), FONT_UI, c->text_muted, 70, false);
        cell(ui, SV(""), FONT_UI, c->text_muted, 20, false);
        cell(ui, tr(ui, "report.played", "Played"), FONT_UI, c->text_muted, 80, false);
        cell(ui, tr(ui, "report.best", "Best"), FONT_UI, c->text_muted, 80, false);
        cell(ui, SV(""), FONT_UI, c->text_muted, 0, false);
        cell(ui, tr(ui, "report.win_loss", "Win rate lost"), FONT_UI, c->text_muted, 110, true);
        cell(ui, tr(ui, "report.points_lost", "Points lost"), FONT_UI, c->text_muted, 100, true);
    }
    for (i32 i = 0; i < n; ++i) {
        GameNode *node = rows[i];
        char played[8], best[8] = "—";
        point_to_gtp(node->move.point, node->board.width, node->board.height, played);
        const NodeEval *before = &node->parent->eval;
        if (before->top_count > 0) point_to_gtp(before->top[0], node->board.width, node->board.height, best);
        f32 points = 0;
        bool has_points = game_node_score_loss(node, &points);
        Clay_ElementId id = CLAY_IDI("DeepMistake", (u32)i);
        bool over = Clay_PointerOver(id);
        f32 h = anim_towards(&ui->anim, id.id, over ? 1.0f : 0.0f, 18.0f, 0.0f);
        CLAY({ .id = id,
               .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_px) }, .childGap = ui_u16(ui, 10),
                           .padding = { ui_u16(ui, 6), ui_u16(ui, 6), 0, 0 },
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
               .backgroundColor = color_to_clay(color_with_alpha(c->hover, h * c->hover.a / 255.0f)),
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
            cell(ui, arena_format(&ui->frame, "%d", node->move_number), FONT_MONO, c->text, 70, false);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 20)), CLAY_SIZING_FIT(0) } } }) { ui_stone_dot(ui, node->move.color); }
            cell(ui, arena_format(&ui->frame, "%s", played), FONT_MONO, c->candidate_bad, 80, false);
            cell(ui, arena_format(&ui->frame, "%s", best), FONT_MONO, c->candidate_good, 80, false);
            cell(ui, SV(""), FONT_UI, c->text, 0, false);
            cell(ui, arena_format(&ui->frame, "-%.1f%%", loss[i] * 100.0f), FONT_MONO, loss[i] > 0.10f ? c->candidate_bad : c->text, 110, true);
            cell(ui, has_points ? arena_format(&ui->frame, "-%.1f", points) : SV("—"), FONT_MONO, c->text, 100, true);
        }
        if (over && ui->mouse_pressed) app_goto(ui->app, node);
    }
    }
    g_cell_size = 15.0f;
}

// ---- Rank (human network) ------------------------------------------------------------------------------

typedef struct RankGraph {
    DeepReport *d;
    i32 ref;   // rank index compared against
} RankGraph;

// Per move: how likely a player of the reference rank would have played it
// (Black's bars up, White's down; square-root scale so small values show).
static void draw_rank_graph(void *user, Rectangle r) {
    RankGraph *g = user;
    DeepReport *d = g->d;
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    DrawRectangleRec(r, color_with_alpha(c->surface_alt, 0.6f));
    if (d->count < 2) return;
    f32 step = r.width / (f32)(d->count - 1);
    phase_dividers(d, r, step);
    f32 mid = r.y + r.height * 0.5f, half = r.height * 0.5f - 2.0f;
    DrawLineEx((Vector2){ r.x, mid }, (Vector2){ r.x + r.width, mid }, 1.0f, color_with_alpha(c->text_muted, 0.4f));
    f32 bar = fmaxf(1.0f, fminf(step * 0.7f, 8.0f * ui->scale));
    for (isize i = 0; i < d->count; ++i) {
        if (!d->has_probs[i]) continue;
        f32 p = d->probs[i][g->ref];
        f32 h = fmaxf(1.0f, half * sqrtf(CLAMP(p, 0.0f, 1.0f)));
        bool black = d->line[i]->move.color == STONE_BLACK;
        Color col = p < 0.02f ? c->candidate_bad : c->human_policy;
        DrawRectangleRec((Rectangle){ r.x + step * (f32)i - bar * 0.5f, black ? mid - h : mid, bar, h }, color_with_alpha(col, 0.85f));
    }
    for (isize i = 0; i < d->count; ++i) {
        if (d->line[i] != ui->app->tree->current) continue;
        f32 x = r.x + step * (f32)i;
        DrawLineEx((Vector2){ x, r.y }, (Vector2){ x, r.y + r.height }, 1.5f, c->accent);
    }
}

static void rank_card(DeepReport *d, Stone color, i32 ref) {
    Ui *ui = d->ui;
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    const RankTally *t = &app->rank.total[color];
    i32 est = rank_tally_best(t);
    f32 post[RANK_COUNT];
    rank_tally_posterior(t, post);
    // Per-move comparison with the reference rank and the player's own estimate.
    f64 sum_ref = 0, sum_est = 0;
    i32 moves = 0, closer = 0, unlikely = 0;
    isize worst[3] = { -1, -1, -1 };
    for (isize i = 0; i < d->count; ++i) {
        if (!d->has_probs[i] || d->line[i]->move.color != color) continue;
        f32 pr = d->probs[i][ref], pe = est >= 0 ? d->probs[i][est] : 0.0f;
        sum_ref += pr;
        sum_est += pe;
        moves++;
        if (pr > pe) closer++;
        if (pr < 0.02f) unlikely++;
        for (i32 k = 0; k < 3; ++k) {   // the three moves the reference rank would least play
            if (worst[k] < 0 || pr < d->probs[worst[k]][ref]) {
                for (i32 m = 2; m > k; --m) worst[m] = worst[m - 1];
                worst[k] = i;
                break;
            }
        }
    }
    StringView ref_label = rank_label(ref);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 10), .padding = CLAY_PADDING_ALL(ui_u16(ui, 12)) },
           .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            ui_stone_dot(ui, color);
            ui_label(ui, player_name(ui, color), FONT_UI_BOLD, 16, c->text);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
            if (est >= 0) {
                CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_label(est))), ui_text_style(ui, FONT_UI_BOLD, 22, c->human_policy));
                CLAY_TEXT(ui_fmt(ui, "%.0f%%", post[est] * 100.0f), ui_text_style(ui, FONT_MONO, 13, c->text_muted));
            }
        }
        // Posterior over ranks: the estimate is filled, the reference rank outlined.
        f32 bar_h = ui_px(ui, 64);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 3) } }) {
            for (i32 r = 0; r < RANK_COUNT; ++r) {
                f32 h = fmaxf(ui_px(ui, 2), bar_h * fminf(1.0f, post[r] / fmaxf(est >= 0 ? post[est] : 1.0f, 1e-6f)));
                Clay_BorderWidth outline = { 0 };
                if (r == ref) outline = (Clay_BorderWidth)CLAY_BORDER_OUTSIDE(2);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                   .childGap = ui_u16(ui, 3), .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_h) }, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h) } },
                               .backgroundColor = color_to_clay(r == est ? c->human_policy : color_with_alpha(c->human_policy, 0.35f)),
                               .border = { .color = color_to_clay(c->accent), .width = outline },
                               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
                    }
                    ui_label(ui, rank_label(r), FONT_MONO, 11, r == ref ? c->accent : c->text_muted);
                }
            }
        }
        // Comparison with the reference rank.
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10) } }) {
            stat_cell(ui, tr_rank(ui, "report.rank_ref_plays", "{rank} would play", ref_label),
                      moves ? arena_format(&ui->frame, "%.1f%%", 100.0 * sum_ref / moves) : SV("—"), c->text);
            stat_cell(ui, tr(ui, "report.rank_own", "Own rank would play"),
                      moves && est >= 0 ? arena_format(&ui->frame, "%.1f%%", 100.0 * sum_est / moves) : SV("—"), c->text);
            stat_cell(ui, tr_rank(ui, "report.rank_closer", "More {rank}-like", ref_label),
                      moves ? arena_format(&ui->frame, "%.0f%%", 100.0 * closer / moves) : SV("—"), c->text);
            stat_cell(ui, tr_rank(ui, "report.rank_unlikely_for", "Unlikely for {rank}", ref_label),
                      arena_format(&ui->frame, "%d", unlikely), unlikely ? c->candidate_bad : c->text);
        }
        // Per phase.
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 14) } }) {
            for (i32 ph = 0; ph < GAME_PHASES; ++ph) {
                i32 r = rank_tally_best(&app->rank.phase[ph][color]);
                CLAY({ .layout = { .childGap = ui_u16(ui, 5), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                    ui_label(ui, tr(ui, PHASE_KEYS[ph], PHASE_FALLBACK[ph]), FONT_UI, 13, c->text_muted);
                    ui_label(ui, r >= 0 ? rank_label(r) : SV("—"), FONT_UI_BOLD, 15, c->text);
                }
            }
        }
        // The moves the reference rank would least play.
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            ui_label(ui, tr_rank(ui, "report.rank_least", "Least {rank}-like", ref_label), FONT_UI, 13, c->text_muted);
            for (i32 k = 0; k < 3; ++k) {
                if (worst[k] < 0) continue;
                GameNode *node = d->line[worst[k]];
                char coord[8];
                point_to_gtp(node->move.point, node->board.width, node->board.height, coord);
                Clay_ElementId id = CLAY_IDI("RankLeast", (u32)(color * 8 + k));
                bool over = Clay_PointerOver(id);
                CLAY({ .id = id, .layout = { .childGap = ui_u16(ui, 5), .padding = { ui_u16(ui, 7), ui_u16(ui, 7), ui_u16(ui, 2), ui_u16(ui, 2) } },
                       .backgroundColor = color_to_clay(over ? c->hover : c->surface),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                    CLAY_TEXT(ui_fmt(ui, "%d %s", node->move_number, coord), ui_text_style(ui, FONT_MONO, 13, c->text));
                    CLAY_TEXT(ui_fmt(ui, "%.1f%%", d->probs[worst[k]][ref] * 100.0f), ui_text_style(ui, FONT_MONO, 13, c->candidate_bad));
                }
                if (over && ui->mouse_pressed) app_goto(app, node);
            }
        }
    }
}

static void rank_page(DeepReport *d) {
    Ui *ui = d->ui;
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    if (d->prob_moves == 0) {
        ui_label(ui, tr(ui, "report.rank_none", "No rank estimate for this game yet."), FONT_UI, 15, c->text_muted);
        ui_command_button_ex(ui, SV("analysis.rank"), SV("@rank"), true, BUTTON_PRIMARY, 20);
        return;
    }
    i32 ref = CLAMP(app->config.report_compare_rank, 0, RANK_COUNT - 1);
    // Reference rank picker.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .padding = { 0, ui_u16(ui, 8), 0, 0 } } }) {
            ui_label(ui, tr(ui, "report.compare_with", "Compare with"), FONT_UI_BOLD, 14, c->text);
        }
        for (i32 r = 0; r < RANK_COUNT; ++r) {
            if (ui_button(ui, CLAY_IDI("RankRef", (u32)r), rank_label(r), BUTTON_NORMAL, r == ref, true)) app->config.report_compare_rank = r;
        }
        ui_help_button(ui, SV("rank_compare"));
    }
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12) } }) {
        rank_card(d, STONE_BLACK, ref);
        rank_card(d, STONE_WHITE, ref);
    }
    CLAY({ .layout = { .childGap = ui_u16(ui, 12), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        legend_dot(ui, c->human_policy, tr_rank(ui, "report.rank_graph", "How likely a {rank} plays each move (Black up, White down)", rank_label(ref)));
        legend_dot(ui, c->candidate_bad, tr(ui, "report.rank_unlikely", "under 2%"));
    }
    RankGraph *g = arena_new(&ui->frame, RankGraph);
    *g = (RankGraph){ d, ref };
    Clay_ElementId id = CLAY_ID("RankGraph");
    CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)), .custom = { .customData = ui_custom(ui, draw_rank_graph, g) } }) {}
    Clay_ElementData data = Clay_GetElementData(id);
    if (data.found && d->count > 1 && ui->mouse_down && Clay_PointerOver(id)) {
        f32 t = (ui->mouse.x - data.boundingBox.x) / fmaxf(1.0f, data.boundingBox.width);
        app_goto(app, d->line[(isize)lroundf(CLAMP(t, 0.0f, 1.0f) * (f32)(d->count - 1))]);
    }
}

// ---- All moves -----------------------------------------------------------------------------------------

// Every analysed move, filterable by player and by size of the loss.
static void moves_page(DeepReport *d) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    i32 *filter = &ui->deep_report_filter;   // bit 0: Black only, bit 1: White only, bit 2: losses >= 2 points
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        if (ui_button(ui, CLAY_ID("MovesAll"), tr(ui, "report.filter_all", "Both players"), BUTTON_NORMAL, (*filter & 3) == 0, true)) *filter &= ~3;
        if (ui_button(ui, CLAY_ID("MovesBlack"), player_name(ui, STONE_BLACK), BUTTON_NORMAL, (*filter & 3) == 1, true)) *filter = (*filter & ~3) | 1;
        if (ui_button(ui, CLAY_ID("MovesWhite"), player_name(ui, STONE_WHITE), BUTTON_NORMAL, (*filter & 3) == 2, true)) *filter = (*filter & ~3) | 2;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 16)), CLAY_SIZING_FIT(0) } } }) {}
        if (ui_button(ui, CLAY_ID("MovesLosses"), tr(ui, "report.filter_losses", "Only losses of 2+ points"), BUTTON_NORMAL, (*filter & 4) != 0, true)) *filter ^= 4;
    }
    g_cell_size = 14.0f;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                       .padding = { ui_u16(ui, 6), ui_u16(ui, 6), 0, ui_u16(ui, 4) } },
           .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
        cell(ui, tr(ui, "label.move", "Move"), FONT_UI, c->text_muted, 60, false);
        cell(ui, SV(""), FONT_UI, c->text_muted, 20, false);
        cell(ui, tr(ui, "report.played", "Played"), FONT_UI, c->text_muted, 70, false);
        cell(ui, tr(ui, "report.best", "Best"), FONT_UI, c->text_muted, 70, false);
        cell(ui, tr(ui, "report.winrate_after", "Black win rate"), FONT_UI, c->text_muted, 0, true);
        cell(ui, tr(ui, "report.win_loss", "Win rate lost"), FONT_UI, c->text_muted, 110, true);
        cell(ui, tr(ui, "report.points_lost", "Points lost"), FONT_UI, c->text_muted, 100, true);
        cell(ui, tr(ui, "report.accuracy_score", "accuracy"), FONT_UI, c->text_muted, 80, true);
        cell(ui, tr(ui, "report.search_share", "Search share"), FONT_UI, c->text_muted, 100, true);
    }
    CLAY({ .id = CLAY_ID("MovesTable"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        i32 shown = 0;
        for (isize i = 0; i < d->count; ++i) {
            GameNode *node = d->line[i];
            f32 wr_loss = 0, points = 0;
            if (!node->has_move || !game_node_winrate_loss(node, &wr_loss)) continue;
            bool has_points = game_node_score_loss(node, &points);
            if ((*filter & 1) && node->move.color != STONE_BLACK) continue;
            if ((*filter & 2) && node->move.color != STONE_WHITE) continue;
            if ((*filter & 4) && (!has_points || points < 2.0f)) continue;
            shown++;
            char played[8], best[8] = "—";
            point_to_gtp(node->move.point, node->board.width, node->board.height, played);
            const NodeEval *before = &node->parent->eval;
            bool matched = before->top_count > 0 && point_eq(before->top[0], node->move.point);
            if (before->top_count > 0) point_to_gtp(before->top[0], node->board.width, node->board.height, best);
            Clay_ElementId id = CLAY_IDI("MovesRow", (u32)i);
            bool over = Clay_PointerOver(id), current = node == ui->app->tree->current;
            Color loss_color = points >= 5.0f ? c->candidate_bad : points >= 2.0f ? mid_loss_color(c) : c->text;
            CLAY({ .id = id,
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 30)) }, .childGap = ui_u16(ui, 10),
                               .padding = { ui_u16(ui, 6), ui_u16(ui, 6), 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = color_to_clay(current ? color_with_alpha(c->accent, 0.14f) : over ? c->hover : (Color){ 0 }),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
                cell(ui, arena_format(&ui->frame, "%d", node->move_number), FONT_MONO, c->text, 60, false);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 20)), CLAY_SIZING_FIT(0) } } }) { ui_stone_dot(ui, node->move.color); }
                cell(ui, arena_format(&ui->frame, "%s", played), FONT_MONO, matched ? c->candidate_good : c->text, 70, false);
                cell(ui, arena_format(&ui->frame, "%s", best), FONT_MONO, c->text_muted, 70, false);
                cell(ui, arena_format(&ui->frame, "%.1f%%", node->eval.black_winrate * 100.0f), FONT_MONO, c->text, 0, true);
                cell(ui, arena_format(&ui->frame, "%+.1f%%", -wr_loss * 100.0f), FONT_MONO, wr_loss > 0.10f ? c->candidate_bad : c->text, 110, true);
                cell(ui, has_points ? arena_format(&ui->frame, "%+.1f", -points) : SV("—"), FONT_MONO, loss_color, 100, true);
                cell(ui, has_points ? arena_format(&ui->frame, "%.0f", move_accuracy(points)) : SV("—"), FONT_MONO, c->text, 80, true);
                f32 share = 0;
                cell(ui, node_played_share(node, &share) ? arena_format(&ui->frame, "%.0f%%", share * 100.0f) : SV("—"), FONT_MONO, c->text, 100, true);
            }
            if (over && ui->mouse_pressed) app_goto(ui->app, node);
        }
        if (shown == 0) ui_label(ui, tr(ui, "report.no_mistakes", "No analysed moves yet: run Analyze game."), FONT_UI, 15, c->text_muted);
    }
    g_cell_size = 15.0f;
}

// ---- Dialog ------------------------------------------------------------------------------------------

static void block(Ui *ui, StringView title, StringView help) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                       .padding = { 0, 0, ui_u16(ui, 4), 0 } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) { section_title(ui, title); }
        if (help.len) ui_help_button(ui, help);
    }
}

static void body(Ui *ui, void *user) {
    DeepReport *d = user;
    const ThemeColors *c = &ui->theme->colors;
    // Progress / actions.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
            isize done = 0, total = 0;
            app_game_analysis_progress(ui->app, &done, &total);
            if (app_game_analysis_running(ui->app)) {
                CLAY_TEXT(ui_fmt(ui, SV_FMT " %td / %td", SV_ARG(tr(ui, "label.analyzing", "Analyzing")), done, total),
                          ui_text_style(ui, FONT_UI, 13, c->text_muted));
            } else {
                CLAY_TEXT(ui_fmt(ui, "%d / %d " SV_FMT, d->report.analysed_nodes, (i32)d->count, SV_ARG(tr(ui, "report.analysed", "positions analysed"))),
                          ui_text_style(ui, FONT_UI, 13, c->text_muted));
            }
        }
        ui_command_button_ex(ui, SV("analysis.rank"), SV("@rank"), true, BUTTON_NORMAL, 10);
        ui_command_button_ex(ui, SV("analysis.game"), SV("@chart"), true, BUTTON_NORMAL, 11);
        ui_help_button(ui, SV("deep_report"));
    }
    // Tabs: one kind of data per page.
    enum { TAB_OVERVIEW, TAB_RANK, TAB_GRAPHS, TAB_DISTRIBUTION, TAB_MISTAKES, TAB_MOVES, TAB_COUNT };
    static const char *const TAB_KEYS[TAB_COUNT] = { "report.tab_overview", "report.tab_rank", "report.tab_graphs", "report.tab_distribution",
                                                     "report.tab_mistakes", "report.tab_moves" };
    static const char *const TAB_FALLBACK[TAB_COUNT] = { "Overview", "Rank", "Graphs", "Distribution", "Mistakes", "All moves" };
    i32 tab = CLAMP(ui->deep_report_tab, 0, TAB_COUNT - 1);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4),
                       .padding = { 0, 0, 0, ui_u16(ui, 6) } },
           .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
        for (i32 t = 0; t < TAB_COUNT; ++t) {
            if (ui_button(ui, CLAY_IDI("DeepTab", (u32)t), tr(ui, TAB_KEYS[t], TAB_FALLBACK[t]), BUTTON_GHOST, t == tab, true)) {
                ui->deep_report_tab = t;
            }
        }
    }
    // Fixed-height page so switching tabs never resizes the dialog; long pages scroll inside.
    f32 page_h = fminf(ui_px(ui, 560), (f32)GetScreenHeight() * 0.64f);
    d->page_h = page_h;
    CLAY({ .id = CLAY_ID("DeepPage"),
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(page_h) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 12) },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        switch (tab) {
            case TAB_OVERVIEW:
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12) } }) {
                    summary_column(d, STONE_BLACK);
                    summary_column(d, STONE_WHITE);
                }
                block(ui, tr(ui, "report.by_phase", "BY PHASE"), SV(""));
                phase_table(d);
                break;
            case TAB_RANK: rank_page(d); break;
            case TAB_MOVES: moves_page(d); break;
            case TAB_GRAPHS:
                block(ui, tr(ui, "report.points_per_move", "POINTS LOST PER MOVE"), SV("loss_graph"));
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, c->text_muted, tr(ui, "report.legend_small", "< 2 points"));
                    legend_dot(ui, mid_loss_color(c), tr(ui, "report.legend_mistake", "2–5 points"));
                    legend_dot(ui, c->candidate_bad, tr(ui, "report.legend_blunder", "≥ 5 points"));
                    ui_label(ui, tr(ui, "report.legend_sides", "Black above the line, White below"), FONT_UI, 13, c->text_muted);
                }
                graph(d, false, 0, 0);
                block(ui, tr(ui, "report.accuracy_trend", "ACCURACY TREND (LAST 10 MOVES)"), SV(""));
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, c->winrate_black, player_name(ui, STONE_BLACK));
                    legend_dot(ui, c->score_line, player_name(ui, STONE_WHITE));
                }
                graph(d, true, 1, 0);
                break;
            case TAB_DISTRIBUTION:
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, c->winrate_black, player_name(ui, STONE_BLACK));
                    legend_dot(ui, c->score_line, player_name(ui, STONE_WHITE));
                }
            {
                // Ten rows (two tables of five) share what is left after the titles.
                f32 row_px = (page_h - ui_px(ui, 120)) / (f32)(2 * LOSS_BUCKETS);
                block(ui, tr(ui, "report.distribution", "POINTS LOST PER MOVE"), SV(""));
                distribution(d, false, row_px);
                block(ui, tr(ui, "report.distribution_wr", "WIN RATE LOST PER MOVE"), SV(""));
                distribution(d, true, row_px);
            }
                break;
            default:
                block(ui, tr(ui, "report.biggest", "BIGGEST MISTAKES"), SV(""));
                mistakes(d);
                break;
        }
    }
}

void deep_report_dialog(Ui *ui) {
    DeepReport *d = arena_new(&ui->frame, DeepReport);
    d->ui = ui;
    d->line = arena_new_array(&ui->frame, GameNode *, DEEP_MAX_LINE);
    d->count = MIN(game_tree_active_line(ui->app->tree, d->line, DEEP_MAX_LINE), (isize)DEEP_MAX_LINE);
    game_report_compute(d->line, d->count, &d->report);
    d->probs = arena_new_array(&ui->frame, RankProbs, d->count + 1);
    d->has_probs = arena_new_array(&ui->frame, bool, d->count + 1);
    for (isize i = 0; i < d->count; ++i) {
        const GameNode *node = d->line[i];
        StringView text = node->has_move ? game_node_prop(node, SV(RANK_SGF_PROP)) : SV("");
        d->has_probs[i] = text.len && rank_probs_parse(text, d->probs[i]);
        d->prob_moves += d->has_probs[i];
    }
    ui_modal_shell(ui, tr(ui, "report.deep_title", "Deep report"), 940, body, NULL, d);
}
