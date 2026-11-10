// Deep report (Hawk Eye): a popup with everything the analysed line says about
// both players — summary, per-phase table, points lost per move, accuracy
// trend, loss distribution and the biggest mistakes. Opened with Y or from the
// sidebar Report; clicking a move or a graph jumps there.
#include "ui/ui.h"

#include "core/game_report.h"

#include <math.h>
#include <stdio.h>

enum { DEEP_MAX_LINE = 4096, TREND_WINDOW = 10 };

typedef struct DeepReport {
    Ui *ui;
    GameNode **line;
    isize count;
    GameReport report;
} DeepReport;

static const char *const PHASE_KEYS[GAME_PHASES] = { "report.phase_opening", "report.phase_middle", "report.phase_endgame" };
static const char *const PHASE_FALLBACK[GAME_PHASES] = { "Opening", "Middle", "Endgame" };

// Mid-size losses (2-5 points): amber, nudged towards the theme's "bad" colour.
static Color mid_loss_color(const ThemeColors *c) { return color_lerp((Color){ 232, 160, 40, 255 }, c->candidate_bad, 0.2f); }

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static StringView player_name(Ui *ui, Stone color) {
    StringView name = game_tree_info(ui->app->tree, color == STONE_BLACK ? SV("PB") : SV("PW"));
    if (name.len) return name;
    return color == STONE_BLACK ? tr(ui, "label.black", "Black") : tr(ui, "label.white", "White");
}

static void section_title(Ui *ui, StringView title) {
    CLAY_TEXT(ui_str(ui, title), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 12),
                                                    .textColor = color_to_clay(ui->theme->colors.text_muted), .letterSpacing = 1 }));
}

static void stat_cell(Ui *ui, StringView label, StringView value, Color color) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        ui_label(ui, label, FONT_UI, 11, c->text_muted);
        ui_label(ui, value, FONT_UI_BOLD, 15, color);
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
            stat_cell(ui, tr(ui, "report.errors", "Mistakes/Blunders"), arena_format(&ui->frame, "%d / %d", p->mistakes, p->blunders),
                 p->blunders ? c->candidate_bad : c->text);
        }
    }
}

// ---- Phase table --------------------------------------------------------------------------------------

static void cell(Ui *ui, StringView text, FontRole role, Color color, f32 width, bool right) {
    CLAY({ .layout = { .sizing = { width > 0 ? CLAY_SIZING_FIXED(ui_px(ui, width)) : CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                       .childAlignment = { .x = right ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT } } }) {
        ui_label(ui, text, role, 13, color);
    }
}

static void phase_table(DeepReport *d) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 4) } }) {
        // Header: phase | Black: accuracy best loss | White: accuracy best loss
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .padding = { 0, 0, 0, ui_u16(ui, 4) } },
               .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
            cell(ui, SV(""), FONT_UI, c->text_muted, 0, false);
            for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 18)), CLAY_SIZING_FIT(0) } } }) { ui_stone_dot(ui, color); }
                cell(ui, tr(ui, "report.accuracy_score", "accuracy"), FONT_UI, c->text_muted, 80, true);
                cell(ui, tr(ui, "report.accuracy", "Best move"), FONT_UI, c->text_muted, 80, true);
                cell(ui, tr(ui, "report.avg_score_loss", "Avg points lost"), FONT_UI, c->text_muted, 110, true);
            }
        }
        for (i32 ph = 0; ph < GAME_PHASES; ++ph) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8) } }) {
                cell(ui, tr(ui, PHASE_KEYS[ph], PHASE_FALLBACK[ph]), FONT_UI_BOLD, c->text, 0, false);
                for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                    const StageReport *st = &d->report.player[color].stage[ph];
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 18)), CLAY_SIZING_FIT(0) } } }) {}
                    if (st->moves == 0) {
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 80, true);
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 80, true);
                        cell(ui, SV("—"), FONT_MONO, c->text_muted, 110, true);
                        continue;
                    }
                    cell(ui, arena_format(&ui->frame, "%.1f", st->accuracy), FONT_MONO, c->text, 80, true);
                    cell(ui, arena_format(&ui->frame, "%.0f%%", 100.0f * (f32)st->top1 / (f32)st->moves), FONT_MONO, c->text, 80, true);
                    cell(ui, arena_format(&ui->frame, "%.2f", st->avg_score_loss), FONT_MONO,
                         st->avg_score_loss > 2.0f ? c->candidate_bad : c->text, 110, true);
                }
            }
        }
    }
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
        // Rolling accuracy (last TREND_WINDOW own moves) for each player, 0..100.
        for (f32 v = 25.0f; v < 100.0f; v += 25.0f) {
            f32 y = r.y + r.height * (1.0f - v / 100.0f);
            DrawLineEx((Vector2){ r.x, y }, (Vector2){ r.x + r.width, y }, 1.0f, color_with_alpha(c->text_muted, 0.15f));
        }
        for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
            f32 window[TREND_WINDOW];
            i32 n = 0, head = 0;
            Vector2 prev = { 0 };
            bool has_prev = false;
            Color col = color == STONE_BLACK ? c->winrate_black : c->score_line;
            for (isize i = 0; i < d->count; ++i) {
                const GameNode *node = d->line[i];
                f32 loss = 0;
                if (!node->has_move || node->move.color != color || !game_node_score_loss(node, &loss)) continue;
                window[head] = move_accuracy(loss);
                head = (head + 1) % TREND_WINDOW;
                if (n < TREND_WINDOW) n++;
                f32 sum = 0;
                for (i32 k = 0; k < n; ++k) sum += window[k];
                Vector2 p = { r.x + step * (f32)i, r.y + r.height * (1.0f - (sum / (f32)n) / 100.0f) };
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
    CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, height)) } },
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)), .custom = { .customData = ui_custom(ui, draw_loss_graph, g) } }) {}
    Clay_ElementData data = Clay_GetElementData(id);
    if (data.found && d->count > 1 && ui->mouse_down && Clay_PointerOver(id)) {
        f32 t = (ui->mouse.x - data.boundingBox.x) / fmaxf(1.0f, data.boundingBox.width);
        app_goto(ui->app, d->line[(isize)lroundf(CLAMP(t, 0.0f, 1.0f) * (f32)(d->count - 1))]);
    }
}

static void legend_dot(Ui *ui, Color color, StringView text) {
    CLAY({ .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 10)), CLAY_SIZING_FIXED(ui_px(ui, 10)) } },
               .backgroundColor = color_to_clay(color), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
        ui_label(ui, text, FONT_UI, 11, ui->theme->colors.text_muted);
    }
}

// ---- Distribution ------------------------------------------------------------------------------------

static void distribution(DeepReport *d, bool winrate) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    static const char *const SCORE_LABELS[LOSS_BUCKETS] = { "< 0.5", "0.5–2", "2–5", "5–10", "≥ 10" };
    static const char *const WR_LABELS[LOSS_BUCKETS] = { "< 2%", "2–5%", "5–10%", "10–20%", "≥ 20%" };
    const char *const *LABELS = winrate ? WR_LABELS : SCORE_LABELS;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 5) } }) {
        for (i32 b = 0; b < LOSS_BUCKETS; ++b) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 70)), CLAY_SIZING_FIT(0) } } }) {
                    ui_label(ui, sv(LABELS[b]), FONT_MONO, 12, c->text);
                }
                for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                    const PlayerReport *p = &d->report.player[color];
                    const i32 *hist = winrate ? p->winrate_hist : p->score_hist;
                    f32 share = p->moves ? (f32)hist[b] / (f32)p->moves : 0.0f;
                    Color col = b >= 3 ? c->candidate_bad : b == 2 ? mid_loss_color(c) : (color == STONE_BLACK ? c->winrate_black : c->score_line);
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 14)) }, .childGap = ui_u16(ui, 6),
                                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(fmaxf(share * 0.8f, 0.004f)), CLAY_SIZING_GROW(0) } },
                               .backgroundColor = color_to_clay(color_with_alpha(col, 0.8f)),
                               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
                        CLAY_TEXT(ui_fmt(ui, "%d · %.0f%%", hist[b], share * 100.0f), ui_text_style(ui, FONT_MONO, 11, c->text_muted));
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
        ui_label(ui, tr(ui, "report.no_mistakes", "No analysed moves yet: run Analyze game."), FONT_UI, 13, c->text_muted);
        return;
    }
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
               .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                           .padding = { ui_u16(ui, 6), ui_u16(ui, 6), ui_u16(ui, 3), ui_u16(ui, 3) },
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
    enum { TAB_OVERVIEW, TAB_GRAPHS, TAB_DISTRIBUTION, TAB_MISTAKES, TAB_COUNT };
    static const char *const TAB_KEYS[TAB_COUNT] = { "report.tab_overview", "report.tab_graphs", "report.tab_distribution", "report.tab_mistakes" };
    static const char *const TAB_FALLBACK[TAB_COUNT] = { "Overview", "Graphs", "Distribution", "Mistakes" };
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
    f32 page_h = fminf(ui_px(ui, 540), (f32)GetScreenHeight() * 0.62f);
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
            case TAB_GRAPHS:
                block(ui, tr(ui, "report.points_per_move", "POINTS LOST PER MOVE"), SV("loss_graph"));
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, c->text_muted, tr(ui, "report.legend_small", "< 2 points"));
                    legend_dot(ui, mid_loss_color(c), tr(ui, "report.legend_mistake", "2–5 points"));
                    legend_dot(ui, c->candidate_bad, tr(ui, "report.legend_blunder", "≥ 5 points"));
                    ui_label(ui, tr(ui, "report.legend_sides", "Black above the line, White below"), FONT_UI, 11, c->text_muted);
                }
                graph(d, false, 0, 210);
                block(ui, tr(ui, "report.accuracy_trend", "ACCURACY TREND (LAST 10 MOVES)"), SV(""));
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, c->winrate_black, player_name(ui, STONE_BLACK));
                    legend_dot(ui, c->score_line, player_name(ui, STONE_WHITE));
                }
                graph(d, true, 1, 170);
                break;
            case TAB_DISTRIBUTION:
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, c->winrate_black, player_name(ui, STONE_BLACK));
                    legend_dot(ui, c->score_line, player_name(ui, STONE_WHITE));
                }
                block(ui, tr(ui, "report.distribution", "POINTS LOST PER MOVE"), SV(""));
                distribution(d, false);
                block(ui, tr(ui, "report.distribution_wr", "WIN RATE LOST PER MOVE"), SV(""));
                distribution(d, true);
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
    ui_modal_shell(ui, tr(ui, "report.deep_title", "Deep report"), 940, body, NULL, d);
}
