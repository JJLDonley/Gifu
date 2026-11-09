// Sidebar panels. Each panel is a named renderer; layout.lisp chooses which
// panels appear and in what order.
#include "ui/ui.h"

#include "core/game_report.h"

#include <math.h>
#include <stdio.h>

typedef void (*PanelFn)(Ui *ui);

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static void stone_dot(Ui *ui, Stone color) {
    const ThemeColors *c = &ui->theme->colors;
    Color fill = color == STONE_BLACK ? c->black_stone : c->white_stone;
    Color edge = color == STONE_BLACK ? c->black_stone_edge : c->white_stone_edge;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 14)), CLAY_SIZING_FIXED(ui_px(ui, 14)) } },
           .backgroundColor = color_to_clay(fill),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 7)),
           .border = { .color = color_to_clay(edge), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
}

static void row_begin_end_label(Ui *ui, StringView left, StringView right) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8) } }) {
        ui_label(ui, left, FONT_UI, 13, c->text_muted);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        ui_label(ui, right, FONT_UI_BOLD, 13, c->text);
    }
}

// ---- game_info -----------------------------------------------------------------------------

static void player_row(Ui *ui, Stone color, StringView name, i32 captures) {
    const ThemeColors *c = &ui->theme->colors;
    bool to_play = ui->app->tree->current->to_play == color;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        stone_dot(ui, color);
        StringView fallback = color == STONE_BLACK ? tr(ui, "label.black", "Black") : tr(ui, "label.white", "White");
        ui_label(ui, name.len ? name : fallback, to_play ? FONT_UI_BOLD : FONT_UI, 15, c->text);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        CLAY_TEXT(ui_fmt(ui, SV_FMT " %d", SV_ARG(tr(ui, "label.captures", "Captures")), captures),
                  ui_text_style(ui, FONT_UI, 12, c->text_muted));
    }
}

static void panel_game_info(Ui *ui) {
    App *app = ui->app;
    const GameTree *t = app->tree;
    const Board *b = &t->current->board;
    CLAY(ui_card(ui, CLAY_ID("PanelGameInfo"))) {
        ui_card_title(ui, tr(ui, "panel.game_info", "Game"));
        StringView br = game_tree_info(t, SV("BR")), wr = game_tree_info(t, SV("WR"));
        String black = string_format(SV_FMT "%s" SV_FMT, SV_ARG(game_tree_info(t, SV("PB"))), br.len ? "  " : "", SV_ARG(br));
        String white = string_format(SV_FMT "%s" SV_FMT, SV_ARG(game_tree_info(t, SV("PW"))), wr.len ? "  " : "", SV_ARG(wr));
        player_row(ui, STONE_BLACK, sv_trim(string_sv(&black)), b->captures[STONE_BLACK]);
        player_row(ui, STONE_WHITE, sv_trim(string_sv(&white)), b->captures[STONE_WHITE]);
        string_free(&black);
        string_free(&white);
        StringView event = game_tree_info(t, SV("EV")), date = game_tree_info(t, SV("DT")), result = game_tree_info(t, SV("RE"));
        if (event.len || date.len) {
            CLAY_TEXT(ui_fmt(ui, SV_FMT "%s" SV_FMT, SV_ARG(event), event.len && date.len ? " · " : "", SV_ARG(date)),
                      ui_text_style(ui, FONT_UI, 12, ui->theme->colors.text_muted));
        }
        CLAY_TEXT(ui_fmt(ui, SV_FMT " %.1f  ·  " SV_FMT " %s%s" SV_FMT, SV_ARG(tr(ui, "label.komi", "Komi")),
                         game_tree_komi(t), SV_ARG(tr(ui, "label.rules", "Rules")), string_cstr(&app->config.rules),
                         result.len ? "  ·  " : "", SV_ARG(result)),
                  ui_text_style(ui, FONT_UI, 12, ui->theme->colors.text_muted));
    }
}

// ---- winrate_summary -----------------------------------------------------------------------------

// The bar never blanks: while a new position waits for its first analysis the
// last value is held, and updates ease towards the new value instead of jumping.
// The card always has the same rows, so nothing below it shifts.
static void panel_winrate_summary(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    const Candidate *best = app->analysis_valid ? analysis_best(app->analysis) : NULL;
    const NodeEval *eval = &app->tree->current->eval;
    bool fresh = best || eval->valid;
    if (fresh) {
        f32 target_wr = best ? analysis_black_winrate(app->analysis, best->winrate) : eval->black_winrate;
        f32 target_score = best ? analysis_black_score(app->analysis, best->score_lead) : eval->black_score;
        if (!ui->eval_known) {
            ui->eval_black_winrate = target_wr;
            ui->eval_black_score = target_score;
            ui->eval_known = true;
        } else {
            f32 k = 1.0f - expf(-GetFrameTime() * 12.0f);
            ui->eval_black_winrate += (target_wr - ui->eval_black_winrate) * k;
            ui->eval_black_score += (target_score - ui->eval_black_score) * k;
        }
    }
    bool known = ui->eval_known;
    f32 black = known ? ui->eval_black_winrate : 0.5f;
    f32 score = ui->eval_black_score;

    CLAY(ui_card(ui, CLAY_ID("PanelSummary"))) {
        ui_card_title(ui, tr(ui, "panel.winrate_summary", "Evaluation"));
        // Win-rate bar: black part | white part.
        f32 bar_h = ui_px(ui, 26);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_h) } },
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)),
               .clip = { .horizontal = true },
               .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(CLAMP(black, 0.02f, 0.98f)), CLAY_SIZING_GROW(0) },
                               .padding = { ui_u16(ui, 8), 0, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = color_to_clay(known ? c->winrate_black : c->surface_alt) }) {
                if (known && black > 0.15f) CLAY_TEXT(ui_fmt(ui, "%.1f%%", black * 100.0f), ui_text_style(ui, FONT_UI_BOLD, 13, c->winrate_white));
            }
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 0, ui_u16(ui, 8), 0, 0 },
                               .childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = color_to_clay(known ? c->winrate_white : c->surface_alt) }) {
                if (known && black < 0.85f) CLAY_TEXT(ui_fmt(ui, "%.1f%%", (1.0f - black) * 100.0f), ui_text_style(ui, FONT_UI_BOLD, 13, c->winrate_black));
            }
        }
        char lead[32] = "—";
        if (known) snprintf(lead, sizeof(lead), "%s+%.1f", score >= 0 ? "B" : "W", score >= 0 ? score : -score);
        row_begin_end_label(ui, tr(ui, "label.score", "Score"), sv(lead));
        StringView status = !app->main.engine ? tr(ui, "label.no_engine", "No engine")
                            : app->analysis_enabled ? tr(ui, "label.analyzing", "Analyzing")
                                                    : SV("—");
        if (fresh) {
            i32 visits = best ? app->analysis->total_visits : eval->visits;
            CLAY_TEXT(ui_fmt(ui, SV_FMT " %d", SV_ARG(tr(ui, "label.visits", "Visits")), visits), ui_text_style(ui, FONT_UI, 12, c->text_muted));
        } else {
            ui_label(ui, status, FONT_UI, 12, c->text_muted);
        }
    }
}

// ---- winrate_graph ---------------------------------------------------------------------------------

static void panel_winrate_graph(Ui *ui) {
    CLAY(ui_card(ui, CLAY_ID("PanelGraph"))) {
        ui_card_title(ui, tr(ui, "panel.winrate_graph", "Win rate"));
        winrate_graph_element(ui, (f32)sexpr_prop_number(ui->panel_node, "height", 150));
    }
}

// ---- move_list -------------------------------------------------------------------------------------------

// Moves of the active line with the eval after each move and the win-rate the
// mover lost; mistakes (> 5%) and blunders (> 10%) are highlighted.
static void panel_move_list(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    enum { MAX_ROWS = 1024 };
    GameNode **line = arena_new_array(&ui->frame, GameNode *, MAX_ROWS);
    isize count = MIN(game_tree_active_line(app->tree, line, MAX_ROWS), (isize)MAX_ROWS);
    Clay_ElementId clip = CLAY_ID("MoveListClip");
    f32 row_h = ui_px(ui, 22);
    CLAY(ui_card(ui, CLAY_ID("PanelMoveList"))) {
        ui_card_title(ui, tr(ui, "panel.move_list", "Moves"));
        CLAY(ui_scroll_area(ui, clip, 180)) {
            for (isize i = 1; i < count; ++i) {
                GameNode *n = line[i];
                Clay_ElementId id = CLAY_IDI("MoveRow", (u32)i);
                bool current = n == app->tree->current;
                bool hovered = Clay_PointerOver(id);
                f32 loss = 0;
                bool has_loss = game_node_winrate_loss(n, &loss);
                Color tint = has_loss && loss > 0.10f ? c->candidate_bad : has_loss && loss > 0.05f ? color_lerp(c->candidate_bad, c->candidate_good, 0.5f) : (Color){ 0 };
                Color bg = current ? color_with_alpha(c->accent, 0.18f) : hovered ? c->hover : tint.a ? color_with_alpha(tint, 0.14f) : (Color){ 0 };
                CLAY({ .id = id,
                       .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_h) }, .childGap = ui_u16(ui, 8),
                                   .padding = { ui_u16(ui, 6), ui_u16(ui, 6), 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                       .backgroundColor = color_to_clay(bg),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 34)), CLAY_SIZING_FIT(0) } } }) {
                        CLAY_TEXT(ui_fmt(ui, "%d", n->move_number), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
                    }
                    if (n->has_move) stone_dot(ui, n->move.color);
                    char coord[8] = "—";
                    if (n->has_move) point_to_gtp(n->move.point, n->board.width, n->board.height, coord);
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 44)), CLAY_SIZING_FIT(0) } } }) {
                        ui_label(ui, sv(coord), FONT_MONO, 13, c->text);
                    }
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 56)), CLAY_SIZING_FIT(0) } } }) {
                        if (n->eval.valid) CLAY_TEXT(ui_fmt(ui, "%.1f%%", n->eval.black_winrate * 100.0f), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
                    }
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
                        if (has_loss && fabsf(loss) >= 0.005f) {
                            CLAY_TEXT(ui_fmt(ui, "%+.1f", -loss * 100.0f), ui_text_style(ui, FONT_MONO, 12, tint.a ? tint : c->text_muted));
                        }
                    }
                }
                if (hovered && ui->mouse_pressed && !ui->pointer_over_menu) app_goto(app, n);
            }
        }
    }
    // Keep the current move in view.
    Clay_ScrollContainerData scroll = Clay_GetScrollContainerData(clip);
    if (scroll.found && scroll.scrollPosition && ui->move_list_scrolled_to != app->tree->current) {
        for (isize i = 1; i < count; ++i) {
            if (line[i] != app->tree->current) continue;
            f32 y = row_h * (f32)(i - 1), vh = scroll.scrollContainerDimensions.height;
            Clay_Vector2 *pos = scroll.scrollPosition;
            if (y + pos->y > vh - row_h) pos->y = -(y - vh + row_h * 2);
            if (y + pos->y < 0) pos->y = -y;
            break;
        }
        ui->move_list_scrolled_to = app->tree->current;
    }
}

// ---- compare (two engines) -------------------------------------------------------------------------------

static void compare_column(Ui *ui, const EngineSlot *slot, const AnalysisSnapshot *a, bool valid, StringView title, u32 col) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 4) } }) {
        ui_label(ui, title, FONT_UI, 11, c->text_muted);
        ui_label(ui, slot->profile ? string_sv(&slot->profile->name) : SV("—"), FONT_UI_BOLD, 13, c->text);
        const Candidate *best = valid ? analysis_best(a) : NULL;
        // Note: no early return inside a CLAY body (it would skip closing the element).
        if (!best) {
            ui_label(ui, slot->status == ENGINE_STATUS_STARTING ? tr(ui, "label.engine_starting", "Engine starting…")
                         : tr(ui, "label.analyzing", "Analyzing"),
                     FONT_UI, 12, c->text_muted);
        } else {
        f32 wr = analysis_black_winrate(a, best->winrate), score = analysis_black_score(a, best->score_lead);
        CLAY_TEXT(ui_fmt(ui, "B %.1f%%  %s+%.1f", wr * 100.0f, score >= 0 ? "B" : "W", fabsf(score)), ui_text_style(ui, FONT_MONO, 13, c->text));
        CLAY_TEXT(ui_fmt(ui, "%d " SV_FMT, a->total_visits, SV_ARG(tr(ui, "label.visits", "Visits"))), ui_text_style(ui, FONT_UI, 11, c->text_muted));
        for (i32 i = 0; i < MIN(a->candidate_count, 5); ++i) {
            const Candidate *cand = &a->candidates[i];
            char coord[8];
            point_to_gtp(cand->move, a->width, a->height, coord);
            CLAY({ .id = CLAY_IDI("CompareRow", col * 8 + (u32)i), .layout = { .childGap = ui_u16(ui, 8) } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 36)), CLAY_SIZING_FIT(0) } } }) {
                    ui_label(ui, sv(coord), FONT_MONO, 12, i == 0 ? c->candidate_best : c->text);
                }
                CLAY_TEXT(ui_fmt(ui, "%.1f%%", cand->winrate * 100.0f), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
            }
        }
        }
    }
}

// Main vs second engine on the current position. The best moves are compared:
// when the engines disagree, the header says so.
static void panel_compare(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    CLAY(ui_card(ui, CLAY_ID("PanelCompare"))) {
        ui_card_title(ui, tr(ui, "panel.compare", "Compare engines"));
        CLAY(ui_scroll_area(ui, CLAY_ID("CompareScroll"), 0)) {
            if (!app->second.engine) {
                ui_label(ui, tr(ui, "compare.hint", "Run a second engine to compare its view of each position."), FONT_UI, 13, c->text_muted);
                CLAY({ .layout = { .padding = { 0, 0, ui_u16(ui, 8), 0 } } }) {
                    ui_command_button_ex(ui, SV("engine.second"), SV("@play"), true, BUTTON_NORMAL, 0);
                }
            } else {
                const Candidate *a = app->analysis_valid ? analysis_best(app->analysis) : NULL;
                const Candidate *b = app->second.analysis_valid ? analysis_best(app->second.analysis) : NULL;
                if (a && b) {
                    bool agree = point_eq(a->move, b->move);
                    ui_label(ui, agree ? tr(ui, "compare.agree", "Both engines agree on the best move")
                                       : tr(ui, "compare.disagree", "The engines disagree"),
                             FONT_UI_BOLD, 13, agree ? c->candidate_good : c->candidate_bad);
                }
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12) } }) {
                    compare_column(ui, &app->main, app->analysis, app->analysis_valid, tr(ui, "compare.main", "Main"), 0);
                    compare_column(ui, &app->second, app->second.analysis, app->second.analysis_valid, tr(ui, "compare.second", "Second"), 1);
                }
                CLAY({ .layout = { .childGap = ui_u16(ui, 6), .padding = { 0, 0, ui_u16(ui, 8), 0 } } }) {
                    ui_command_button_ex(ui, SV("game.match"), SV("@play"), true, BUTTON_NORMAL, 0);
                    ui_command_button_ex(ui, SV("engine.second"), SV("@close"), true, BUTTON_GHOST, 1);
                }
            }
        }
    }
}

// ---- report (Hawk Eye) -------------------------------------------------------------------------------------

static void report_stat(Ui *ui, StringView label, StringView value, Color color) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        ui_label(ui, label, FONT_UI, 11, c->text_muted);
        ui_label(ui, value, FONT_UI_BOLD, 16, color);
    }
}

static void report_player(Ui *ui, const PlayerReport *p, Stone color, StringView name) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 6), .padding = { 0, 0, ui_u16(ui, 4), ui_u16(ui, 8) } } }) {
        CLAY({ .layout = { .childGap = ui_u16(ui, 8), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            stone_dot(ui, color);
            ui_label(ui, name, FONT_UI_BOLD, 14, c->text);
            CLAY_TEXT(ui_fmt(ui, "· %d %s", p->moves, p->moves == 1 ? "move" : "moves"), ui_text_style(ui, FONT_UI, 12, c->text_muted));
            i32 rank = ui->app->rank_valid ? rank_tally_best(&ui->app->rank_result[color]) : -1;
            if (rank >= 0) {   // human-network rank estimate (details further down)
                CLAY({ .layout = { .padding = { ui_u16(ui, 6), ui_u16(ui, 6), ui_u16(ui, 1), ui_u16(ui, 1) } },
                       .backgroundColor = color_to_clay(color_with_alpha(ui->theme->colors.human_policy, 0.15f)),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                    CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_label(rank))), ui_text_style(ui, FONT_UI_BOLD, 12, c->human_policy));
                }
            }
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8) } }) {
            report_stat(ui, tr(ui, "report.accuracy", "Best move"), arena_format(&ui->frame, "%.0f%%", player_accuracy(p) * 100.0f), c->text);
            report_stat(ui, tr(ui, "report.top3", "Top 3"),
                        arena_format(&ui->frame, "%.0f%%", p->moves ? 100.0f * (f32)p->top3 / (f32)p->moves : 0.0f), c->text);
            report_stat(ui, tr(ui, "report.avg_loss", "Avg loss"), arena_format(&ui->frame, "%.1f%%", p->avg_winrate_loss * 100.0f), c->text);
            report_stat(ui, tr(ui, "report.errors", "Mistakes/Blunders"), arena_format(&ui->frame, "%d / %d", p->mistakes, p->blunders),
                        p->blunders ? c->candidate_bad : c->text);
        }
        for (i32 i = 0; i < p->worst_count; ++i) {
            GameNode *n = p->worst[i];
            char coord[8];
            point_to_gtp(n->move.point, n->board.width, n->board.height, coord);
            Clay_ElementId id = CLAY_IDI("ReportWorst", (u32)(color * 16 + i));
            bool over = Clay_PointerOver(id);
            f32 h = anim_towards(&ui->anim, id.id, over ? 1.0f : 0.0f, 18.0f, 0.0f);
            CLAY({ .id = id,
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                               .padding = { ui_u16(ui, 6), ui_u16(ui, 6), ui_u16(ui, 3), ui_u16(ui, 3) } },
                   .backgroundColor = color_to_clay(color_with_alpha(c->hover, h * c->hover.a / 255.0f)),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
                CLAY_TEXT(ui_fmt(ui, SV_FMT " %d", SV_ARG(tr(ui, "label.move", "Move")), n->move_number), ui_text_style(ui, FONT_UI, 13, c->text));
                ui_label(ui, sv(coord), FONT_MONO, 13, c->text_muted);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
                    CLAY_TEXT(ui_fmt(ui, "-%.1f%%", p->worst_loss[i] * 100.0f),
                              ui_text_style(ui, FONT_MONO, 13, p->worst_loss[i] > 0.10f ? c->candidate_bad : c->text_muted));
                }
            }
            if (over && ui->mouse_pressed && !ui->pointer_over_menu) app_goto(ui->app, n);
        }
    }
}

// Per-player summary of the analysed line: agreement with the engine's best /
// top-3 moves, average win-rate loss, mistakes and blunders, worst moves.
// Human-network rank estimate: label, confidence and the posterior over ranks as small bars.
static void report_rank_row(Ui *ui, Stone color, StringView name) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    const RankTally *t = &app->rank_result[color];
    i32 best = rank_tally_best(t);
    f32 post[RANK_COUNT];
    rank_tally_posterior(t, post);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        stone_dot(ui, color);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) { ui_label(ui, name, FONT_UI, 13, c->text); }
        if (best >= 0) {
            CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_label(best))), ui_text_style(ui, FONT_UI_BOLD, 14, c->text));
            CLAY_TEXT(ui_fmt(ui, "%.0f%%", post[best] * 100.0f), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
        } else {
            ui_label(ui, SV("—"), FONT_UI, 13, c->text_muted);
        }
    }
    if (best < 0) return;
    // Posterior bars, 20k (left) .. 9d (right).
    f32 bar_h = ui_px(ui, 22);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_h) }, .childGap = ui_u16(ui, 2),
                       .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
        for (i32 r = 0; r < RANK_COUNT; ++r) {
            f32 h = fmaxf(1.0f, bar_h * fminf(1.0f, post[r] / fmaxf(post[best], 1e-6f)));
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h) } },
                   .backgroundColor = color_to_clay(r == best ? c->human_policy : color_with_alpha(c->human_policy, 0.35f)),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
        }
    }
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) { ui_label(ui, rank_label(0), FONT_MONO, 11, c->text_muted); }
        ui_label(ui, rank_label(RANK_COUNT - 1), FONT_MONO, 11, c->text_muted);
    }
}

static void report_ranks(Ui *ui, StringView black, StringView white) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 6), .padding = { 0, 0, ui_u16(ui, 6), ui_u16(ui, 4) } },
           .border = { .color = color_to_clay(c->border), .width = { .top = 1 } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 0, 0, ui_u16(ui, 6), 0 } } }) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                i32 done = 0, total = 0;
                if (app_rank_progress(app, &done, &total)) {
                    CLAY_TEXT(ui_fmt(ui, SV_FMT " %d / %d", SV_ARG(tr(ui, "report.rank_running", "Estimating ranks")), done, total),
                              ui_text_style(ui, FONT_UI, 13, c->text_muted));
                } else {
                    ui_label(ui, tr(ui, "report.rank", "Rank (human network)"), FONT_UI_BOLD, 13, c->text);
                }
            }
            ui_command_button_ex(ui, SV("analysis.rank"), SV(""), true, BUTTON_NORMAL, 0);
        }
        if (app->rank_valid) {
            report_rank_row(ui, STONE_BLACK, black);
            report_rank_row(ui, STONE_WHITE, white);
        }
    }
}

static void panel_report(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    enum { MAX_LINE = 4096 };
    GameNode **line = arena_new_array(&ui->frame, GameNode *, MAX_LINE);
    isize count = MIN(game_tree_active_line(app->tree, line, MAX_LINE), (isize)MAX_LINE);
    GameReport *report = arena_new(&ui->frame, GameReport);
    game_report_compute(line, count, report);
    CLAY(ui_card(ui, CLAY_ID("PanelReport"))) {
        ui_card_title(ui, tr(ui, "panel.report", "Report"));
        CLAY(ui_scroll_area(ui, CLAY_ID("ReportScroll"), 0)) {
            // Progress / call to action.
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 0, 0, 0, ui_u16(ui, 6) } } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                    isize done = 0, total = 0;
                    app_game_analysis_progress(app, &done, &total);
                    if (app_game_analysis_running(app)) {
                        CLAY_TEXT(ui_fmt(ui, SV_FMT " %td / %td", SV_ARG(tr(ui, "label.analyzing", "Analyzing")), done, total),
                                  ui_text_style(ui, FONT_UI, 13, c->text_muted));
                    } else {
                        CLAY_TEXT(ui_fmt(ui, "%d / %d " SV_FMT, report->analysed_nodes, (i32)count, SV_ARG(tr(ui, "report.analysed", "positions analysed"))),
                                  ui_text_style(ui, FONT_UI, 13, c->text_muted));
                    }
                }
                ui_command_button(ui, SV("analysis.game"), SV("@chart"), BUTTON_NORMAL, 0);
            }
            if (app_game_analysis_running(app)) {
                isize done = 0, total = 0;
                app_game_analysis_progress(app, &done, &total);
                f32 progress = total ? (f32)done / (f32)total : 0.0f;
                f32 shown = anim_towards(&ui->anim, 0x9B07u, progress, 8.0f, progress);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 4)) } },
                       .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(CLAMP(shown, 0.001f, 1.0f)), CLAY_SIZING_GROW(0) } },
                           .backgroundColor = color_to_clay(c->accent), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
                }
            }
            StringView black = game_tree_info(app->tree, SV("PB")), white = game_tree_info(app->tree, SV("PW"));
            report_player(ui, &report->player[STONE_BLACK], STONE_BLACK, black.len ? black : tr(ui, "label.black", "Black"));
            report_player(ui, &report->player[STONE_WHITE], STONE_WHITE, white.len ? white : tr(ui, "label.white", "White"));
            report_ranks(ui, black.len ? black : tr(ui, "label.black", "Black"), white.len ? white : tr(ui, "label.white", "White"));
        }
    }
}

// ---- candidates ---------------------------------------------------------------------------------------

static void panel_candidates(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    ui->hover_list_candidate = -1;
    CLAY(ui_card(ui, CLAY_ID("PanelCandidates"))) {
        ui_card_title(ui, tr(ui, "panel.candidates", "Candidate moves"));
        if (!app->analysis_valid || app->analysis->candidate_count == 0) {
            ui_label(ui, SV("—"), FONT_UI, 13, c->text_muted);
        } else {
            const AnalysisSnapshot *a = app->analysis;
            i32 rows = ui->in_tab ? a->candidate_count : MIN(a->candidate_count, 8);
            CLAY(ui_scroll_area(ui, CLAY_ID("CandidateScroll"), 0))
            for (i32 i = 0; i < rows; ++i) {
                const Candidate *cand = &a->candidates[i];
                char coord[8];
                point_to_gtp(cand->move, a->width, a->height, coord);
                Clay_ElementId id = CLAY_IDI("CandidateRow", i);
                bool hovered = Clay_PointerOver(id);
                if (hovered) ui->hover_list_candidate = i;
                CLAY({ .id = id,
                       .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                                   .padding = { ui_u16(ui, 6), ui_u16(ui, 6), ui_u16(ui, 3), ui_u16(ui, 3) },
                                   .childGap = ui_u16(ui, 10) },
                       .backgroundColor = color_to_clay(hovered ? c->hover : (Color){ 0 }),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 40)), CLAY_SIZING_FIT(0) } } }) {
                        ui_label(ui, sv(coord), FONT_MONO, 13, i == 0 ? c->candidate_best : c->text);
                    }
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 60)), CLAY_SIZING_FIT(0) } } }) {
                        CLAY_TEXT(ui_fmt(ui, "%.1f%%", cand->winrate * 100.0f), ui_text_style(ui, FONT_MONO, 13, c->text));
                    }
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 56)), CLAY_SIZING_FIT(0) } } }) {
                        CLAY_TEXT(ui_fmt(ui, "%+.1f", cand->score_lead), ui_text_style(ui, FONT_MONO, 13, c->text));
                    }
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
                        CLAY_TEXT(ui_fmt(ui, "%d", cand->visits), ui_text_style(ui, FONT_MONO, 13, c->text_muted));
                    }
                }
                if (hovered && ui->mouse_released && ui->active_id == 0) app_play(app, cand->move);
            }
        }
    }
}

// ---- comment -------------------------------------------------------------------------------------------

// Writes the editor text back into the node's C[] property.
static void commit_comment(Ui *ui) {
    GameNode *node = ui->comment_node;
    if (!node) return;
    StringView old = game_node_prop(node, SV("C"));
    StringView text = string_sv(&ui->comment.text);
    if (sv_eq(old, text)) return;
    if (text.len) game_node_set_prop(ui->app->tree, node, SV("C"), text);
    else game_node_remove_prop(node, SV("C"));
    ui->app->dirty = true;
}

static void panel_comment(Ui *ui) {
    GameNode *current = ui->app->tree->current;
    bool focused = ui->focus_id == CLAY_ID("CommentEditor").id;
    if (ui->comment_node != current) {
        if (focused) commit_comment(ui);   // navigated away while editing
        ui->comment_node = current;
        ui->comment.multiline = true;
        text_edit_set(&ui->comment, game_node_prop(current, SV("C")));
    }
    CLAY(ui_card(ui, CLAY_ID("PanelComment"))) {
        ui_card_title(ui, tr(ui, "panel.comment", "Comment"));
        CLAY(ui_scroll_area(ui, CLAY_ID("CommentScroll"), 0)) {
            TextInputResult r = ui_text_input(ui, CLAY_ID("CommentEditor"), &ui->comment, 14, 3, tr(ui, "label.no_comment", "No comment"));
            if (r == TEXT_INPUT_COMMITTED) commit_comment(ui);
            if (r == TEXT_INPUT_CANCELLED) text_edit_set(&ui->comment, game_node_prop(current, SV("C")));
        }
    }
}

// ---- variation_tree ---------------------------------------------------------------------------------

static void panel_variation_tree(Ui *ui) {
    CLAY(ui_card(ui, CLAY_ID("PanelVariations"))) {
        ui_card_title(ui, tr(ui, "panel.variation_tree", "Variations"));
        variation_tree_element(ui, 120);
    }
}

// ---- engine_log ----------------------------------------------------------------------------------------

static void panel_engine_log(Ui *ui) {
    App *app = ui->app;
    CLAY(ui_card(ui, CLAY_ID("PanelEngineLog"))) {
        ui_card_title(ui, tr(ui, "panel.engine_log", "Engine log"));
        CLAY(ui_scroll_area(ui, CLAY_ID("EngineLogScroll"), 160)) {
            i32 start = MAX(0, app->log_count - 80);
            for (i32 i = start; i < app->log_count; ++i) {
                const String *line = &app->log_lines[(app->log_head + i) % APP_LOG_LINES];
                ui_label(ui, string_sv(line), FONT_MONO, 11, ui->theme->colors.text_muted);
            }
        }
    }
}

// ---- Registry --------------------------------------------------------------------------------------------

static const struct {
    const char *name;
    PanelFn render;
} PANELS[] = {
    { "game_info", panel_game_info },
    { "winrate_summary", panel_winrate_summary },
    { "winrate_graph", panel_winrate_graph },
    { "candidates", panel_candidates },
    { "comment", panel_comment },
    { "variation_tree", panel_variation_tree },
    { "move_list", panel_move_list },
    { "report", panel_report },
    { "compare", panel_compare },
    { "engine_log", panel_engine_log },
};

void panels_render(Ui *ui, StringView name) {
    for (i32 i = 0; i < (i32)countof(PANELS); ++i) {
        if (sv_eq(name, sv(PANELS[i].name))) {
            PANELS[i].render(ui);
            return;
        }
    }
    ui_label(ui, name, FONT_UI, 12, ui->theme->colors.danger);
}
