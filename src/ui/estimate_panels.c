// Territory estimate and confidence widgets: KataGo's ownership map counted per
// side (core/estimate.c), eased between positions and held while a new position
// waits for its first analysis, so the numbers never blank or jump.
#include "ui/ui.h"

#include <math.h>
#include <string.h>

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static bool territory_rules(const App *app) {
    StringView r = string_sv(&app->config.rules);
    return sv_eq(r, SV("japanese")) || sv_eq(r, SV("korean"));
}

// The current estimate (eased), or false when there has never been one. Asks
// for ownership from the engine the first time a widget needs it.
static bool estimate(Ui *ui, TerritoryEstimate *out) {
    App *app = ui->app;
    if (!app->ownership_wanted) {
        app->ownership_wanted = true;
        if (app->analysis_enabled) app_set_analysis(app, true);   // re-issue with ownership
    }
    const AnalysisSnapshot *a = app->analysis_valid ? app->analysis : NULL;
    const Candidate *best = a ? analysis_best(a) : NULL;
    const Board *board = &app->tree->current->board;
    if (a && a->has_ownership && best && a->width == board->width && a->height == board->height) {
        f32 own[BOARD_MAX_POINTS];
        i32 n = board->width * board->height;
        f32 sign = a->to_play == STONE_WHITE ? -1.0f : 1.0f;   // the map is from the side to move's view
        for (i32 i = 0; i < n; ++i) own[i] = a->ownership[i] * sign;
        TerritoryEstimate target = territory_estimate(board, own, game_tree_komi(app->tree), analysis_black_score(a, best->score_lead),
                                                      territory_rules(app));
        if (!ui->estimate_known) {
            ui->estimate_shown = target;
            ui->estimate_known = true;
        } else {
            f32 k = 1.0f - expf(-GetFrameTime() * 10.0f);
            f32 *s = &ui->estimate_shown.black;
            const f32 *t = &target.black;
            for (usize i = 0; i < sizeof(TerritoryEstimate) / sizeof(f32); ++i) s[i] += (t[i] - s[i]) * k;
        }
    }
    *out = ui->estimate_shown;
    return ui->estimate_known;
}

static void no_estimate(Ui *ui) {
    ui_label(ui, ui->app->main.engine ? tr(ui, "estimate.waiting", "Waiting for KataGo's analysis…")
                                      : tr(ui, "estimate.needs_engine", "Needs KataGo analysis"),
             FONT_UI, 12, ui->theme->colors.text_muted);
}

static void side_row(Ui *ui, Stone color, StringView name, f32 points, bool leader) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        f32 d = ui_px(ui, 12);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(d), CLAY_SIZING_FIXED(d) } },
               .backgroundColor = color_to_clay(color == STONE_BLACK ? c->winrate_black : c->winrate_white), .cornerRadius = CLAY_CORNER_RADIUS(d * 0.5f),
               .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
        ui_label(ui, name, FONT_UI, 13, c->text);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        CLAY_TEXT(ui_fmt(ui, "%.1f", points), ui_text_style(ui, leader ? FONT_UI_BOLD : FONT_UI, 15, c->text));
    }
}

void panel_territory(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    TerritoryEstimate e;
    bool known = estimate(ui, &e);
    CLAY(ui_card(ui, CLAY_ID("PanelTerritory"))) {
        ui_card_title_help(ui, tr(ui, "panel.territory", "Territory estimate"), SV("territory"));
        if (!known) {
            no_estimate(ui);
        } else {
            side_row(ui, STONE_BLACK, tr(ui, "label.black", "Black"), e.black, e.black >= e.white);
            side_row(ui, STONE_WHITE, tr(ui, "label.white", "White"), e.white, e.white > e.black);
            f32 lead = e.black - e.white;
            CLAY_TEXT(ui_fmt(ui, "%s+%.1f  ·  " SV_FMT " %.1f  ·  " SV_FMT, lead >= 0 ? "B" : "W", fabsf(lead), SV_ARG(tr(ui, "label.komi", "Komi")),
                             game_tree_komi(ui->app->tree),
                             SV_ARG(territory_rules(ui->app) ? tr(ui, "estimate.territory_counting", "territory counting")
                                                             : tr(ui, "estimate.area_counting", "area counting"))),
                      ui_text_style(ui, FONT_UI, 12, c->text_muted));
        }
    }
}

// One segment of the confidence bar (skipped when empty).
static void segment(Ui *ui, f32 share, Color color) {
    if (share <= 0.0005f) return;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(share), CLAY_SIZING_GROW(0) } }, .backgroundColor = color_to_clay(color) }) {}
}

static void legend(Ui *ui, Color color, StringView text) {
    CLAY({ .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        f32 d = ui_px(ui, 10);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(d), CLAY_SIZING_FIXED(d) } }, .backgroundColor = color_to_clay(color),
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)), .border = { .color = color_to_clay(ui->theme->colors.border), .width = CLAY_BORDER_OUTSIDE(1) } }) {}
        ui_label(ui, text, FONT_UI, 11, ui->theme->colors.text_muted);
    }
}

void panel_confidence(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    TerritoryEstimate e;
    bool known = estimate(ui, &e);
    Color black = c->winrate_black, white = c->winrate_white;
    // Distinct in every theme: likely = each side's colour pulled towards the other,
    // unclaimed = a neutral grey between them.
    Color black_likely = color_lerp(black, white, 0.35f), white_likely = color_lerp(white, black, 0.18f);
    Color unclaimed = color_lerp(c->surface_alt, c->text_muted, 0.35f);
    CLAY(ui_card(ui, CLAY_ID("PanelConfidence"))) {
        ui_card_title_help(ui, tr(ui, "panel.confidence", "Confidence"), SV("confidence"));
        if (!known) {
            no_estimate(ui);
        } else {
            // Secure | likely | unclaimed | likely | secure, as shares of all counted points.
            f32 total = e.black_secure + e.black_likely + e.unclaimed + e.white_likely + e.white_secure;
            f32 inv = total > 0 ? 1.0f / total : 0;
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 22)) }, .childGap = 1 },   // 1 px dividers
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)), .clip = { .horizontal = true }, .backgroundColor = color_to_clay(c->border),
                   .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                segment(ui, e.black_secure * inv, black);
                segment(ui, e.black_likely * inv, black_likely);
                segment(ui, e.unclaimed * inv, unclaimed);
                segment(ui, e.white_likely * inv, white_likely);
                segment(ui, e.white_secure * inv, white);
            }
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10) } }) {
                legend(ui, black, arena_format(&ui->frame, "%.0f", e.black_secure));
                legend(ui, black_likely, arena_format(&ui->frame, "%.0f", e.black_likely));
                legend(ui, unclaimed, arena_format(&ui->frame, "%.0f", e.unclaimed));
                legend(ui, white_likely, arena_format(&ui->frame, "%.0f", e.white_likely));
                legend(ui, white, arena_format(&ui->frame, "%.0f", e.white_secure));
            }
            StringView decided = tr(ui, "estimate.decided", "Board decided");
            CLAY_TEXT(ui_fmt(ui, SV_FMT ": %.0f%%", SV_ARG(decided), e.decided * 100.0f), ui_text_style(ui, FONT_UI_BOLD, 13, c->text));
        }
    }
}
