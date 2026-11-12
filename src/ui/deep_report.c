// Deep report (Hawk Eye): a popup with everything the analysed line says about
// both players — summary, per-phase table, points lost per move, accuracy
// trend, loss distribution and the biggest mistakes. Opened with Y or from the
// sidebar Report; clicking a move or a graph jumps there.
#include "ui/ui.h"

#include "core/style.h"

#include "core/game_report.h"

#include <math.h>
#include <stdlib.h>
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

// A player's bar colour, lifted towards the text colour when it would vanish into
// the page (Black's colour on dark themes).
static f32 luma(Color c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f; }
// The two stay apart: White's is moved towards the text colour when they look alike.
static Color player_bar_color(const ThemeColors *c, Stone color) {
    Color b = c->winrate_black, w = c->score_line;
    if (fabsf(luma(b) - luma(c->surface)) < 0.25f) b = color_lerp(c->surface, c->text, 0.35f);
    if (fabsf(luma(w) - luma(c->surface)) < 0.25f) w = color_lerp(c->surface, c->text, 0.6f);
    if (fabsf(luma(w) - luma(b)) < 0.2f) w = color_lerp(w, luma(c->text) > luma(b) ? c->text : c->surface, 0.6f);
    return color == STONE_BLACK ? b : w;
}

// Translated text with "{rank}" replaced by a rank label ("9d").
static StringView tr_rank(Ui *ui, const char *key, const char *fallback, StringView rank) {
    StringView text = tr(ui, key, fallback);
    isize at = sv_find(text, SV("{rank}"));
    if (at < 0) return text;
    return arena_format(&ui->frame, SV_FMT SV_FMT SV_FMT, SV_ARG(sv_take(text, at)), SV_ARG(rank), SV_ARG(sv_drop(text, at + 6)));
}

static void style_page(DeepReport *d, const StyleCounts *black, const StyleCounts *white);

// The Profile dialog shows a profile with the same views: "you as Black / White".
static bool g_profile_mode;
// Edit mode (the Deep report's Edit button, for all its tabs): settings such as the
// match rule, the filters and the Distribution limits become inputs; otherwise text.
static bool g_editing;

static StringView player_name(Ui *ui, Stone color) {
    if (g_profile_mode) {
        return color == STONE_BLACK ? tr(ui, "profile.as_black", "You as Black") : tr(ui, "profile.as_white", "You as White");
    }
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

// Logical font size that fits a row of `row_px` pixels.
static f32 font_for_row(Ui *ui, f32 row_px, f32 lo, f32 hi) { return CLAMP(row_px / ui->scale * 0.42f, lo, hi); }

// A typed number (digits and a point only; Enter or a click elsewhere applies it)
// followed by - / + (Shift: 10 steps; the mouse wheel over the field works too).
static f32 value_stepper(Ui *ui, u32 index, f32 value, f32 step, f32 lo, f32 hi, const char *fmt) {
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) step *= 10.0f;
    Clay_ElementId id = CLAY_IDI("DeepStepValue", index);
    CLAY({ .layout = { .childGap = ui_u16(ui, 4), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        f64 v = value;
        if (ui_number_input(ui, id, &v, lo, hi, fmt, 58, 12.6f, true)) value = (f32)v;
        if (ui_button(ui, CLAY_IDI("DeepStepMinus", index), SV("−"), BUTTON_NORMAL, false, value > lo + 1e-4f)) value -= step;
        if (ui_button(ui, CLAY_IDI("DeepStepPlus", index), SV("+"), BUTTON_NORMAL, false, value < hi - 1e-4f)) value += step;
    }
    if (Clay_PointerOver(id) && ui->wheel != 0 && ui->focus_id != id.id) value += (ui->wheel > 0 ? 1.0f : -1.0f) * step;
    return CLAMP(value, lo, hi);
}

static i32 move_stepper(Ui *ui, u32 index, i32 value, i32 lo, i32 hi) {
    return (i32)lroundf(value_stepper(ui, 20 + index, (f32)value, 1.0f, (f32)lo, (f32)hi, "%.0f"));
}

// ---- Overview: a butterfly chart of both players ------------------------------------------------------

// Report ranges: the whole game, a phase, or moves n..N (games only, not profiles).
enum { RANGE_ALL, RANGE_OPENING, RANGE_MIDDLE, RANGE_ENDGAME, RANGE_CUSTOM, RANGE_COUNT };

static StageReport overall_of(const PlayerReport *p) {
    return (StageReport){ .moves = p->moves, .top1 = p->top1, .top3 = p->top3, .match_moves = p->match_moves, .matches = p->matches,
                          .mistakes = p->mistakes, .blunders = p->blunders,
                          .agreement = p->agreement, .agreement_moves = p->agreement_moves, .accuracy = p->accuracy,
                          .avg_winrate_loss = p->avg_winrate_loss, .avg_score_loss = p->avg_score_loss, .score_moves = p->score_moves };
}

// The two players' numbers for the chosen range. Custom ranges are computed from
// the line's moves n..N.
static void range_stats(DeepReport *d, i32 range, i32 from, i32 to, StageReport out[3]) {
    for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
        const PlayerReport *p = &d->report.player[color];
        out[color] = range == RANGE_ALL || range == RANGE_CUSTOM ? overall_of(p) : p->stage[range - RANGE_OPENING];
    }
    if (range != RANGE_CUSTOM) return;
    isize first = -1, count = 0;
    for (isize i = 0; i < d->count; ++i) {
        i32 m = d->line[i]->move_number;
        if (m < from || m > to) continue;
        if (first < 0) first = i;
        count = i - first + 1;
    }
    GameReport *part = arena_new(&d->ui->frame, GameReport);
    if (first >= 0) game_report_compute(d->line + first, count, part);
    for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) out[color] = overall_of(&part->player[color]);
}

// One butterfly row: the label in the middle, Black's bar growing to the left and
// White's to the right, each with its value outside the bar. The better value is bold.
typedef struct ButterflyRow {
    StringView label;
    f32 value[3];        // by Stone
    bool known[3];
    const char *format;  // printf format of a value
    f32 scale;           // the value a full bar stands for (0: the larger of the two)
    bool lower_better;
} ButterflyRow;

static void butterfly_row(Ui *ui, const ButterflyRow *row, f32 row_px, f32 font, u32 index) {
    const ThemeColors *c = &ui->theme->colors;
    f32 scale = row->scale > 0 ? row->scale : fmaxf(fmaxf(row->value[STONE_BLACK], row->value[STONE_WHITE]), 1e-6f);
    bool both = row->known[STONE_BLACK] && row->known[STONE_WHITE];
    f32 b = row->value[STONE_BLACK], w = row->value[STONE_WHITE];
    Stone better = !both || fabsf(b - w) < 1e-6f ? STONE_EMPTY : (b < w) == row->lower_better ? STONE_BLACK : STONE_WHITE;
    CLAY({ .id = CLAY_IDI("DeepButterfly", index),
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_px) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
           .border = { .color = color_to_clay(color_with_alpha(c->border, 0.4f)), .width = { .bottom = 1 } } }) {
        for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
            bool left = color == STONE_BLACK;
            if (!left) {   // the label, centred between the two halves
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, font * 8.1f)), CLAY_SIZING_FIT(0) },
                                   .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
                    ui_label(ui, row->label, FONT_UI, font * 0.81f, c->text_muted);
                }
            }
            f32 v = row->value[color];
            f32 share = row->known[color] ? CLAMP(v / scale, 0.0f, 1.0f) : 0.0f;
            Color col = player_bar_color(c, color);
            Clay_String text = row->known[color] ? ui_fmt(ui, row->format, (f64)v) : ui_str(ui, SV("—"));
            Clay_TextElementConfig *style = ui_text_style(ui, better == color ? FONT_UI_BOLD : FONT_MONO, font, better == color ? c->text : c->text_muted);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_px * 0.56f) }, .childGap = ui_u16(ui, 8),
                               .childAlignment = { .x = left ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER } } }) {
                if (left) CLAY_TEXT(text, style);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(fmaxf(share * 0.72f, 0.004f)), CLAY_SIZING_GROW(0) } },
                       .backgroundColor = color_to_clay(color_with_alpha(col, 0.85f)),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
                if (!left) CLAY_TEXT(text, style);
            }
        }
    }
}

// Player names over the two halves, with the estimated rank (games only).
static void butterfly_header(DeepReport *d, const StageReport st[3]) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
            bool left = color == STONE_BLACK;
            i32 rank = ui->app->rank_valid && !g_profile_mode ? rank_tally_best(&ui->app->rank.total[color]) : -1;
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                               .childAlignment = { .x = left ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER } } }) {
                if (!left) {
                    CLAY_TEXT(ui_fmt(ui, "%d %s", st[color].moves, st[color].moves == 1 ? "move" : "moves"), ui_text_style(ui, FONT_UI, 12, c->text_muted));
                }
                if (!left && rank >= 0) CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_estimate_label(rank))), ui_text_style(ui, FONT_UI_BOLD, 13, c->human_policy));
                if (left) ui_stone_dot(ui, color);
                ui_label(ui, player_name(ui, color), FONT_UI_BOLD, 16, c->text);
                if (!left) ui_stone_dot(ui, color);
                if (left && rank >= 0) CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_estimate_label(rank))), ui_text_style(ui, FONT_UI_BOLD, 13, c->human_policy));
                if (left) {
                    CLAY_TEXT(ui_fmt(ui, "%d %s", st[color].moves, st[color].moves == 1 ? "move" : "moves"), ui_text_style(ui, FONT_UI, 12, c->text_muted));
                }
            }
        }
    }
}


// The range buttons (and the custom range's move numbers). Returns the range in use.
static i32 range_bar(DeepReport *d, i32 *from, i32 *to, i32 last_move) {
    Ui *ui = d->ui;
    Config *cfg = &ui->app->config;
    static const char *const KEYS[RANGE_COUNT] = { "report.range_all", "report.phase_opening", "report.phase_middle", "report.phase_endgame",
                                                    "report.range_custom" };
    static const char *const FALLBACK[RANGE_COUNT] = { "Overall", "Opening", "Middle", "Endgame", "Custom" };
    i32 count = g_profile_mode ? RANGE_CUSTOM : RANGE_COUNT;
    i32 range = CLAMP(cfg->report_range, 0, count - 1);
    i32 points = game_tree_width(ui->app->tree) * game_tree_height(ui->app->tree);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (i32 r = 0; r < count; ++r) {
            if (ui_button(ui, CLAY_IDI("DeepRange", (u32)r), tr(ui, KEYS[r], FALLBACK[r]), BUTTON_NORMAL, r == range, true)) cfg->report_range = range = r;
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        if (range == RANGE_CUSTOM) {
            i32 hi = MAX(last_move, 1);
            i32 a = CLAMP(cfg->report_range_from, 1, hi), b = cfg->report_range_to > 0 ? CLAMP(cfg->report_range_to, 1, hi) : hi;
            ui_label(ui, tr(ui, "report.range_moves", "Moves"), FONT_UI, 13, ui->theme->colors.text_muted);
            if (g_editing) {
                a = move_stepper(ui, 0, a, 1, hi);
                ui_label(ui, SV("–"), FONT_UI, 13, ui->theme->colors.text_muted);
                b = move_stepper(ui, 1, b, 1, hi);
            } else {
                ui_label(ui, arena_format(&ui->frame, "%d–%d", a, b), FONT_UI, 13, ui->theme->colors.text_muted);
            }
            if (a > b) {   // the one being changed pushes the other
                if (a != cfg->report_range_from) b = a;
                else a = b;
            }
            cfg->report_range_from = a, cfg->report_range_to = b;
            *from = a, *to = b;
        } else if (range != RANGE_ALL) {   // the phase's moves
            i32 ph = range - RANGE_OPENING;
            i32 a = game_phase_start(ph, points), b = ph + 1 < GAME_PHASES ? game_phase_start(ph + 1, points) - 1 : 0;
            CLAY_TEXT(b ? ui_fmt(ui, SV_FMT " %d–%d", SV_ARG(tr(ui, "report.range_moves", "Moves")), a, b)
                        : ui_fmt(ui, SV_FMT " %d+", SV_ARG(tr(ui, "report.range_moves", "Moves")), a),
                      ui_text_style(ui, FONT_UI, 13, ui->theme->colors.text_muted));
        }
    }
    return range;
}

static void overview(DeepReport *d) {
    Ui *ui = d->ui;
    i32 last_move = 0;
    for (isize i = 0; i < d->count; ++i) last_move = MAX(last_move, d->line[i]->move_number);
    i32 from = 1, to = last_move;
    i32 range = range_bar(d, &from, &to, last_move);
    StageReport st[3];
    range_stats(d, range, from, to, st);
    butterfly_header(d, st);
    // Hawk Eye's figures (LizzieYzy): accuracy (played visits / best visits), match
    // (top N with at least P% of the best's visits), best move, average losses.
    enum { ROWS = 5 };
    f32 row_px = fmaxf((d->page_h - ui_px(ui, 150)) / (f32)ROWS, ui_px(ui, 28));
    f32 font = font_for_row(ui, row_px, 13.0f, 18.0f);
    ButterflyRow rows[ROWS] = {
        { tr(ui, "report.accuracy_score_title", "Accuracy"), { 0 }, { 0 }, "%.1f", 100.0f, false },
        { tr(ui, "report.match", "Match"), { 0 }, { 0 }, "%.0f%%", 100.0f, false },
        { tr(ui, "report.accuracy", "Best move"), { 0 }, { 0 }, "%.0f%%", 100.0f, false },
        { tr(ui, "report.avg_loss", "Avg loss"), { 0 }, { 0 }, "%.1f%%", 0, true },
        { tr(ui, "report.avg_score_loss", "Avg points lost"), { 0 }, { 0 }, "%.2f", 0, true },
    };
    for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
        const StageReport *s = &st[color];
        bool visits = s->match_moves > 0;
        f32 v[ROWS] = { s->accuracy, visits ? 100.0f * (f32)s->matches / (f32)s->match_moves : 0,
                        visits ? 100.0f * (f32)s->top1 / (f32)s->match_moves : 0, s->avg_winrate_loss * 100.0f, s->avg_score_loss };
        bool known[ROWS] = { visits, visits, visits, s->moves > 0, s->score_moves > 0 };   // no points from Leela
        for (i32 r = 0; r < ROWS; ++r) {
            rows[r].value[color] = v[r];
            rows[r].known[color] = known[r];
        }
    }
    // The Match rule, editable: top N candidates with at least P% of the best's visits.
    {
        Config *cfg = &ui->app->config;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            f32 moves = (f32)cfg->report_match_moves, pct = cfg->report_match_percent;
            if (g_editing) {
                ui_label(ui, tr(ui, "report.match_rule", "Match: one of the top"), FONT_UI, 13, ui->theme->colors.text_muted);
                moves = value_stepper(ui, 10, moves, 1.0f, 1.0f, 361.0f, "%.0f");
                ui_label(ui, tr(ui, "report.match_rule2", "moves, with at least (%)"), FONT_UI, 13, ui->theme->colors.text_muted);
                pct = value_stepper(ui, 11, pct, 5.0f, 0.0f, 100.0f, "%g");
                ui_label(ui, tr(ui, "report.match_rule3", "of the best move's visits"), FONT_UI, 13, ui->theme->colors.text_muted);
            } else {
                ui_label(ui, arena_format(&ui->frame, SV_FMT " %d " SV_FMT " %g%% " SV_FMT, SV_ARG(tr(ui, "report.match_rule", "Match: one of the top")),
                                          cfg->report_match_moves, SV_ARG(tr(ui, "report.match_rule_text", "moves, with at least")),
                                          (f64)cfg->report_match_percent, SV_ARG(tr(ui, "report.match_rule3", "of the best move's visits"))),
                         FONT_UI, 13, ui->theme->colors.text_muted);
            }
            if ((i32)moves != cfg->report_match_moves || fabsf(pct - cfg->report_match_percent) > 1e-4f) {
                cfg->report_match_moves = (i32)moves, cfg->report_match_percent = pct;
                app_apply_loss_limits(ui->app);   // also applies the match rule
            }
        }
    }
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        for (i32 r = 0; r < ROWS; ++r) butterfly_row(ui, &rows[r], row_px, font, (u32)r);
    }
}

// ---- Tables --------------------------------------------------------------------------------------

static f32 g_cell_size = 15.0f;   // font size of table cells (set per table from the space it gets)

static void cell(Ui *ui, StringView text, FontRole role, Color color, f32 width, bool right) {
    CLAY({ .layout = { .sizing = { width > 0 ? CLAY_SIZING_FIXED(ui_px(ui, width)) : CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                       .childAlignment = { .x = right ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT } } }) {
        ui_label(ui, text, role, g_cell_size, color);
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
                f32 acc = 0;
                if (!node->has_move || node->move.color != color || !node_accuracy(node, &acc)) continue;
                window[head] = acc;   // Hawk Eye accuracy of the move
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
               .backgroundColor = color_to_clay(color), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)),
               .border = { .color = color_to_clay(ui->theme->colors.border), .width = CLAY_BORDER_OUTSIDE(1) } }) {}   // light colours on light themes
        ui_label(ui, text, FONT_UI, 13, ui->theme->colors.text_muted);
    }
}

// ---- Distribution ------------------------------------------------------------------------------------


// Moves the start of bucket b (limit b-1) by `delta` (in % or points), keeping the
// limits increasing; saved to the settings (report.*_limits) and applied at once.
// Sets where bucket b starts (in % or points), kept between its neighbours.
static void set_limit(Ui *ui, bool winrate, i32 b, f32 shown) {
    LossLimits l = *loss_limits();
    f32 *lim = winrate ? l.winrate : l.score;
    f32 scale = winrate ? 0.01f : 1.0f, gap = 0.1f * scale;
    i32 i = b - 1;
    f32 lo = i > 0 ? lim[i - 1] + gap : gap, hi = i < LOSS_BUCKETS - 2 ? lim[i + 1] - gap : 100.0f * scale;
    lim[i] = CLAMP(shown * scale, lo, hi);
    loss_limits_set(&l);
    Config *cfg = &ui->app->config;
    String *field = winrate ? &cfg->report_winrate_limits : &cfg->report_score_limits;
    string_free(field);
    *field = loss_limits_format(lim, winrate ? 100.0f : 1.0f);
}

static void distribution(DeepReport *d, bool winrate, f32 row_px) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    const f32 *lim = winrate ? loss_limits()->winrate : loss_limits()->score;
    f32 font = font_for_row(ui, row_px, 12.0f, 18.0f);
    f32 bar_px = row_px * 0.62f;
    // Butterfly chart: the buckets down the middle, Black's bars growing to the left,
    // White's to the right.
    f32 gap = ui_px(ui, 8);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        for (i32 b = 0; b < LOSS_BUCKETS; ++b) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(row_px) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                for (Stone color = STONE_BLACK; color <= STONE_WHITE; ++color) {
                    const PlayerReport *p = &d->report.player[color];
                    const i32 *hist = winrate ? p->winrate_hist : p->score_hist;
                    f32 share = p->moves ? (f32)hist[b] / (f32)p->moves : 0.0f;
                    Color col = b >= 3 ? c->candidate_bad : b == 2 ? mid_loss_color(c) : player_bar_color(c, color);
                    bool left = color == STONE_BLACK;
                    if (!left) {   // the bucket's range, centred between the halves; - / + move where it starts
                        u32 idx = (winrate ? 100u : 0u) + (u32)b;
                        // Fixed columns so every row lines up: the start (typed; the first
                        // row's 0 is read-only), where the bucket ends, then - / +.
                        // Compact controls with room on both sides, so the bars do not crowd them.
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, font * 10.8f)), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 2),
                                           .padding = { ui_u16(ui, 10), ui_u16(ui, 10), 0, 0 },
                                           .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
                            bool editable = b > 0;
                            f32 step = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 5.0f : (winrate ? 1.0f : 0.5f);
                            f32 unit = winrate ? 100.0f : 1.0f;
                            f64 start = b > 0 ? lim[b - 1] * unit : 0.0;
                            Clay_ElementId fid = CLAY_IDI("DistStart", idx);
                            StringView end = b < LOSS_BUCKETS - 1 ? arena_format(&ui->frame, "–%g%s", (f64)(lim[b] * unit), winrate ? "%" : "")
                                                                  : arena_format(&ui->frame, "+%s", winrate ? "%" : "");
                            if (!g_editing) {   // the range as text: "5–10%", "< 2%", "≥ 20%"
                                StringView text = b == 0                 ? arena_format(&ui->frame, "< %g%s", (f64)(lim[0] * unit), winrate ? "%" : "")
                                                  : b == LOSS_BUCKETS - 1 ? arena_format(&ui->frame, "≥ %g%s", (f64)(lim[b - 1] * unit), winrate ? "%" : "")
                                                                         : arena_format(&ui->frame, "%g" SV_FMT, start, SV_ARG(end));
                                ui_label(ui, text, FONT_MONO, font * 0.9f, c->text);
                            } else {
                                if (ui_number_input(ui, fid, &start, 0.0, 100.0, "%g", font * 2.35f, 10.8f, editable)) set_limit(ui, winrate, b, (f32)start);
                                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, font * 2.9f)), CLAY_SIZING_FIT(0) } } }) {
                                    ui_label(ui, end, FONT_MONO, font * 0.8f, c->text);
                                }
                                if (ui_button(ui, CLAY_IDI("DistMinus", idx), SV("−"), BUTTON_SMALL, false, editable) && editable) set_limit(ui, winrate, b, lim[b - 1] * unit - step);
                                if (ui_button(ui, CLAY_IDI("DistPlus", idx), SV("+"), BUTTON_SMALL, false, editable) && editable) set_limit(ui, winrate, b, lim[b - 1] * unit + step);
                                if (editable && ui->wheel != 0 && Clay_PointerOver(fid) && ui->focus_id != fid.id) set_limit(ui, winrate, b, lim[b - 1] * unit + (ui->wheel > 0 ? step : -step));
                            }
                        }
                    }
                    Clay_String count = ui_fmt(ui, "%d · %.0f%%", hist[b], share * 100.0f);
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(bar_px) }, .childGap = (u16)gap,
                                       .childAlignment = { .x = left ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER } } }) {
                        if (left) CLAY_TEXT(count, ui_text_style(ui, FONT_MONO, font * 0.85f, c->text_muted));
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(fmaxf(share * 0.75f, 0.004f)), CLAY_SIZING_GROW(0) } },
                               .backgroundColor = color_to_clay(color_with_alpha(col, 0.8f)),
                               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
                        if (!left) CLAY_TEXT(count, ui_text_style(ui, FONT_MONO, font * 0.85f, c->text_muted));
                    }
                }
            }
        }
    }
}

// ---- Biggest mistakes -----------------------------------------------------------------------------------

// A column title that sorts the table when clicked (the active one is
// highlighted, with a small arrow).
static void sort_header(Ui *ui, StringView label, i32 index, bool active, f32 width, bool right, String *setting, const char *value) {
    const ThemeColors *c = &ui->theme->colors;
    Clay_ElementId id = CLAY_IDI("DeepMistakeSort", (u32)index);
    bool over = Clay_PointerOver(id);
    Color color = active ? c->accent : over ? c->text : c->text_muted;
    CLAY({ .id = id,
           .layout = { .sizing = { width > 0 ? CLAY_SIZING_FIXED(ui_px(ui, width)) : CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 3),
                       .childAlignment = { .x = right ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER } } }) {
        ui_label(ui, label, active ? FONT_UI_BOLD : FONT_UI, g_cell_size, color);
        if (active) ui_icon(ui, icon_find(sv_eq(sv(value), SV("move")) ? SV("up") : SV("down")), 12, color);
    }
    if (over) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    if (over && ui->mouse_pressed) {
        string_free(setting);
        *setting = string_from(sv(value));
    }
}

static i32 by_move_number(const void *a, const void *b) {
    return (*(GameNode *const *)a)->move_number - (*(GameNode *const *)b)->move_number;
}


// The moves the list shows: a win-rate loss of at least report.filter_winrate %
// and a points loss of at least report.filter_points (both, like LizzieYzy's
// List Filter; moves without a points estimate pass on the win rate alone).
static bool listed(const Config *cfg, const GameNode *node, f32 *wr, f32 *points, bool *has_points) {
    if (!node->has_move || !node->parent || !game_node_winrate_loss(node, wr)) return false;
    *has_points = game_node_score_loss(node, points);
    if (*wr * 100.0f < cfg->report_filter_winrate - 1e-4f) return false;
    if (*has_points && *points < cfg->report_filter_points - 1e-4f) return false;
    return true;
}

static void mistakes(DeepReport *d) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    Config *cfg = &ui->app->config;
    // Filters: which moves are listed.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        if (g_editing) {
            ui_label(ui, tr(ui, "report.filter_winrate", "Win rate lost ≥ (%)"), FONT_UI, 13, c->text_muted);
            cfg->report_filter_winrate = value_stepper(ui, 0, cfg->report_filter_winrate, 1.0f, 0.0f, 100.0f, "%g");
            CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 10)), CLAY_SIZING_FIT(0) } } }) {}
            ui_label(ui, tr(ui, "report.filter_points", "Points lost ≥"), FONT_UI, 13, c->text_muted);
            cfg->report_filter_points = value_stepper(ui, 1, cfg->report_filter_points, 0.5f, 0.0f, 100.0f, "%g");
        } else {
            ui_label(ui, arena_format(&ui->frame, SV_FMT " %g%%  ·  " SV_FMT " %g", SV_ARG(tr(ui, "report.filter_winrate_text", "Win rate lost ≥")),
                                      (f64)cfg->report_filter_winrate, SV_ARG(tr(ui, "report.filter_points", "Points lost ≥")), (f64)cfg->report_filter_points),
                     FONT_UI, 13, c->text_muted);
        }
    }
    // Sorted by win rate lost, points lost or move number (a click on the column
    // title; remembered).
    StringView sort = string_sv(&cfg->report_mistakes_sort);
    bool by_move = sv_eq(sort, SV("move")), by_points = sv_eq(sort, SV("points"));
    GameNode **rows = arena_new_array(&ui->frame, GameNode *, d->count + 1);
    f32 *key = arena_new_array(&ui->frame, f32, d->count + 1);
    i32 n = 0;
    for (isize i = 0; i < d->count; ++i) {
        f32 wr = 0, points = 0;
        bool has_points = false;
        if (!listed(cfg, d->line[i], &wr, &points, &has_points)) continue;
        f32 k = by_points ? (has_points ? points : -1e9f) : wr;
        i32 at = n++;
        while (!by_move && at > 0 && key[at - 1] < k) {   // insertion sort, biggest loss first
            rows[at] = rows[at - 1];
            key[at] = key[at - 1];
            --at;
        }
        rows[at] = d->line[i];
        key[at] = k;
    }
    if (by_move) qsort(rows, (size_t)n, sizeof(rows[0]), by_move_number);
    if (d->report.analysed_nodes == 0) {
        ui_label(ui, tr(ui, "report.no_mistakes", "No analysed moves yet: run Analyze game."), FONT_UI, 15, c->text_muted);
        return;
    }
    f32 row_px = ui_px(ui, 34);
    g_cell_size = font_for_row(ui, row_px, 13.0f, 16.0f);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {   // no gap between rows
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                       .padding = { ui_u16(ui, 6), ui_u16(ui, 18), 0, ui_u16(ui, 4) } },
           .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
        sort_header(ui, tr(ui, "label.move", "Move"), 0, by_move, 70, false, &cfg->report_mistakes_sort, "move");
        cell(ui, SV(""), FONT_UI, c->text_muted, 20, false);
        cell(ui, tr(ui, "report.played", "Played"), FONT_UI, c->text_muted, 80, false);
        cell(ui, tr(ui, "report.best", "Best"), FONT_UI, c->text_muted, 80, false);
        cell(ui, arena_format(&ui->frame, "%d %s", n, n == 1 ? "move" : "moves"), FONT_UI, c->text_muted, 0, false);
        sort_header(ui, tr(ui, "report.win_loss", "Win rate lost"), 1, !by_move && !by_points, 140, true, &cfg->report_mistakes_sort, "winrate");
        sort_header(ui, tr(ui, "report.points_lost", "Points lost"), 2, !by_move && by_points, 130, true, &cfg->report_mistakes_sort, "points");
    }
    // Every listed move, in a fixed box that scrolls.
    Clay_ElementId list_id = CLAY_ID("DeepMistakeList");
    CLAY({ .id = list_id,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .padding = { 0, ui_u16(ui, 12), 0, 0 } },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        if (n == 0) ui_label(ui, tr(ui, "report.no_listed", "No move passes these filters."), FONT_UI, 14, c->text_muted);
        for (i32 i = 0; i < n; ++i) {
            GameNode *node = rows[i];
            char played[8], best[8] = "—";
            point_to_gtp(node->move.point, node->board.width, node->board.height, played);
            const NodeEval *before = &node->parent->eval;
            if (before->top_count > 0) point_to_gtp(before->top[0], node->board.width, node->board.height, best);
            f32 points = 0, wr = 0;
            bool has_points = game_node_score_loss(node, &points);
            game_node_winrate_loss(node, &wr);
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
                // Losses with a minus; a move that gained (by the other measure) gets a plus.
                cell(ui, arena_format(&ui->frame, "%+.1f%%", -wr * 100.0f), FONT_MONO, wr > 0.10f ? c->candidate_bad : c->text, 140, true);
                cell(ui, has_points ? arena_format(&ui->frame, "%+.1f", -points) : SV("—"), FONT_MONO, points > 5.0f ? c->candidate_bad : c->text, 130, true);
            }
            if (over && ui->mouse_pressed) app_goto(ui->app, node);
        }
    }
    ui_scrollbar(ui, list_id, 0);
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
                CLAY_TEXT(ui_fmt(ui, "≈ " SV_FMT, SV_ARG(rank_estimate_label(est))), ui_text_style(ui, FONT_UI_BOLD, 22, c->human_policy));
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
                    ui_label(ui, r >= 0 ? rank_estimate_label(r) : SV("—"), FONT_UI_BOLD, 15, c->text);
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
    // The scale: KataGo's human ranks are KGS ranks.
    ui_label(ui, tr(ui, "report.rank_scale", "Ranks are on the KGS scale (KataGo's human network). Fox ranks are about 2 higher at dan level; one game gives roughly ±2 ranks."),
             FONT_UI, 12, c->text_muted);
    // Reference rank picker.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .padding = { 0, ui_u16(ui, 8), 0, 0 } } }) {
            ui_label(ui, tr(ui, "report.compare_with_kgs", "Compare with (KGS)"), FONT_UI_BOLD, 14, c->text);
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
                f32 acc = 0;
                cell(ui, node_accuracy(node, &acc) ? arena_format(&ui->frame, "%.0f", acc) : SV("—"), FONT_MONO, c->text, 80, true);
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
    g_editing = ui->deep_report_editing;
    const ThemeColors *c = &ui->theme->colors;
    // Progress / actions.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
            isize done = 0, total = 0;
            app_game_analysis_progress(ui->app, &done, &total);
            i32 rank_done = 0, rank_total = 0;
            if (app_game_analysis_running(ui->app)) {
                CLAY_TEXT(ui_fmt(ui, SV_FMT " %td / %td", SV_ARG(tr(ui, "label.analyzing", "Analyzing")), done, total),
                          ui_text_style(ui, FONT_UI, 13, c->text_muted));
            } else if (app_rank_progress(ui->app, &rank_done, &rank_total)) {   // results fill in live
                CLAY_TEXT(ui_fmt(ui, SV_FMT " %d / %d", SV_ARG(tr(ui, "report.rank_running", "Estimating ranks")), rank_done, rank_total),
                          ui_text_style(ui, FONT_UI, 13, c->human_policy));
            } else {
                CLAY_TEXT(ui_fmt(ui, "%d / %d " SV_FMT, d->report.analysed_nodes, (i32)d->count, SV_ARG(tr(ui, "report.analysed", "positions analysed"))),
                          ui_text_style(ui, FONT_UI, 13, c->text_muted));
            }
        }
        // Whose analysis the report shows, when more than one engine analysed the game
        // (each engine's evaluations are kept apart).
        GameTree *tree = ui->app->tree;
        if (game_tree_has_evals(tree, EVAL_KATAGO) && game_tree_has_evals(tree, EVAL_LEELA)) {
            for (i32 src = 0; src < EVAL_SOURCES; ++src) {
                if (ui_button(ui, CLAY_IDI("DeepSource", (u32)src), sv(eval_source_name((EvalSource)src)), BUTTON_NORMAL,
                              tree->eval_source == (EvalSource)src, true)) {
                    app_set_eval_source(ui->app, (EvalSource)src);
                }
            }
        }
        ui_command_button_ex(ui, SV("analysis.rank"), SV("@rank"), true, BUTTON_NORMAL, 10);
        ui_command_button_ex(ui, SV("analysis.game"), SV("@chart"), true, BUTTON_NORMAL, 11);
        // Edit: the report's settings (match rule, custom range, filters, Distribution
        // limits) as inputs on every tab; Done shows them as text again.
        StringView edit_label = ui->deep_report_editing ? arena_format(&ui->frame, "@check " SV_FMT, SV_ARG(tr(ui, "report.done", "Done")))
                                                        : arena_format(&ui->frame, "@pen " SV_FMT, SV_ARG(tr(ui, "report.edit", "Edit")));
        if (ui_button(ui, CLAY_ID("DeepEdit"), edit_label, BUTTON_NORMAL, ui->deep_report_editing, true)) ui->deep_report_editing = !ui->deep_report_editing;
        ui_help_button(ui, SV("deep_report"));
    }
    // Tabs: one kind of data per page.
    enum { TAB_OVERVIEW, TAB_RANK, TAB_GRAPHS, TAB_DISTRIBUTION, TAB_STYLE, TAB_MISTAKES, TAB_MOVES, TAB_COUNT };
    static const char *const TAB_KEYS[TAB_COUNT] = { "report.tab_overview", "report.tab_rank", "report.tab_graphs", "report.tab_distribution",
                                                     "report.tab_style", "report.tab_mistakes", "report.tab_moves" };
    static const char *const TAB_FALLBACK[TAB_COUNT] = { "Overview", "Rank", "Graphs", "Distribution", "Style", "Mistakes", "All moves" };
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
            case TAB_OVERVIEW: overview(d); break;
            case TAB_RANK: rank_page(d); break;
            case TAB_STYLE: {
                StyleReport style;
                style_compute(d->line, d->count, &style);
                style_page(d, &style.player[STONE_BLACK], &style.player[STONE_WHITE]);
            } break;
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
                    legend_dot(ui, player_bar_color(c, STONE_BLACK), player_name(ui, STONE_BLACK));
                    legend_dot(ui, player_bar_color(c, STONE_WHITE), player_name(ui, STONE_WHITE));
                }
                graph(d, true, 1, 0);
                break;
            case TAB_DISTRIBUTION:
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, player_bar_color(c, STONE_BLACK), player_name(ui, STONE_BLACK));
                    legend_dot(ui, player_bar_color(c, STONE_WHITE), player_name(ui, STONE_WHITE));
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
                block(ui, tr(ui, "report.biggest", "MISTAKES"), SV(""));
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

// ---- Style ------------------------------------------------------------------------------------------

typedef struct StyleSlider {
    Ui *ui;
    f32 black, white;   // 0..1, < 0 = no data
} StyleSlider;

static void draw_style_slider(void *user, Rectangle r) {
    StyleSlider *s = user;
    Ui *ui = s->ui;
    const ThemeColors *c = &ui->theme->colors;
    f32 y = r.y + r.height * 0.5f, pad = ui_px(ui, 9);
    DrawRectangleRounded((Rectangle){ r.x, y - ui_px(ui, 3), r.width, ui_px(ui, 6) }, 1.0f, 6, c->surface_alt);
    DrawLineEx((Vector2){ r.x + r.width * 0.5f, y - ui_px(ui, 7) }, (Vector2){ r.x + r.width * 0.5f, y + ui_px(ui, 7) }, 1.0f, c->border);
    f32 span = r.width - 2 * pad;
    if (s->white >= 0) {
        Vector2 p = { r.x + pad + span * CLAMP(s->white, 0.0f, 1.0f), y };
        DrawCircleV(p, ui_px(ui, 8), c->winrate_white);
        DrawCircleLinesV(p, ui_px(ui, 8), c->text_muted);
    }
    if (s->black >= 0) DrawCircleV((Vector2){ r.x + pad + span * CLAMP(s->black, 0.0f, 1.0f), y }, ui_px(ui, 8), c->winrate_black);
}

// "Territory  [----o--●----]  Influence" with each side's percentage under it.
static void style_row(Ui *ui, u32 index, StringView left, StringView right, f32 black, f32 white, StringView detail) {
    const ThemeColors *c = &ui->theme->colors;
    StyleSlider *s = arena_new(&ui->frame, StyleSlider);
    *s = (StyleSlider){ ui, black, white };
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 4),
                       .padding = { 0, 0, ui_u16(ui, 6), ui_u16(ui, 6) } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 110)), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
                ui_label(ui, left, FONT_UI_BOLD, 14, c->text);
            }
            CLAY({ .id = CLAY_IDI("StyleSlider", index), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 24)) } },
                   .custom = { .customData = ui_custom(ui, draw_style_slider, s) } }) {}
            CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 110)), CLAY_SIZING_FIT(0) } } }) { ui_label(ui, right, FONT_UI_BOLD, 14, c->text); }
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
            ui_label(ui, detail, FONT_UI, 12, c->text_muted);
        }
    }
}

// ---- Where they play: heat maps and zones ----------------------------------------------------------

typedef struct HeatMap {
    Ui *ui;
    StyleCounts s;   // a copy: the map is drawn after layout, when the caller's counts are gone
    Color color;
} HeatMap;

// A 19x19 grid with each point shaded by how often the player played there (the
// most-played point at full strength), the zone bands outlined.
static void draw_heat_map(void *user, Rectangle r) {
    HeatMap *h = user;
    Ui *ui = h->ui;
    const ThemeColors *c = &ui->theme->colors;
    enum { N = STYLE_HEAT_SIZE };
    f32 side = fminf(r.width, r.height), cell = side / (f32)N;
    Rectangle b = { r.x + (r.width - side) * 0.5f, r.y + (r.height - side) * 0.5f, side, side };
    DrawRectangleRec(b, color_with_alpha(c->surface_alt, 0.8f));
    u16 most = 1;
    for (i32 i = 0; i < N * N; ++i) most = MAX(most, h->s.heat[i]);
    for (i32 y = 0; y < N; ++y) {
        for (i32 x = 0; x < N; ++x) {
            u16 n = h->s.heat[y * N + x];
            if (!n) continue;
            f32 t = sqrtf((f32)n / (f32)most);   // square root: single moves still show
            DrawRectangleRec((Rectangle){ b.x + cell * (f32)x + 0.5f, b.y + cell * (f32)y + 0.5f, cell - 1.0f, cell - 1.0f },
                             color_with_alpha(h->color, 0.15f + 0.8f * t));
        }
    }
    // The corner / side bands.
    f32 band = (f32)style_zone_band(N, N) * cell;
    Color line = color_with_alpha(c->text_muted, 0.35f);
    DrawRectangleLinesEx(b, 1.0f, line);
    DrawRectangleLinesEx((Rectangle){ b.x + band, b.y + band, side - 2 * band, side - 2 * band }, 1.0f, line);
    for (i32 k = 0; k < 2; ++k) {
        f32 p = k ? side - band : band;
        DrawLineEx((Vector2){ b.x + p, b.y }, (Vector2){ b.x + p, b.y + band }, 1.0f, line);
        DrawLineEx((Vector2){ b.x + p, b.y + side - band }, (Vector2){ b.x + p, b.y + side }, 1.0f, line);
        DrawLineEx((Vector2){ b.x, b.y + p }, (Vector2){ b.x + band, b.y + p }, 1.0f, line);
        DrawLineEx((Vector2){ b.x + side - band, b.y + p }, (Vector2){ b.x + side, b.y + p }, 1.0f, line);
    }
}

static void where_card(DeepReport *d, Stone color, const StyleCounts *s) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    static const char *const KEYS[STYLE_ZONES] = { "style.corner", "style.side", "style.center" };
    static const char *const FALLBACK[STYLE_ZONES] = { "Corners", "Sides", "Centre" };
    HeatMap *h = arena_new(&ui->frame, HeatMap);
    *h = (HeatMap){ ui, *s, color == STONE_BLACK ? player_bar_color(c, STONE_BLACK) : c->accent };
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12), .padding = CLAY_PADDING_ALL(ui_u16(ui, 10)) },
           .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 150)), CLAY_SIZING_FIXED(ui_px(ui, 150)) } },
               .custom = { .customData = ui_custom(ui, draw_heat_map, h) } }) {}
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 6) } }) {
            CLAY({ .layout = { .childGap = ui_u16(ui, 6), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
                ui_stone_dot(ui, color);
                ui_label(ui, player_name(ui, color), FONT_UI_BOLD, 14, c->text);
            }
            for (i32 z = 0; z < STYLE_ZONES; ++z) {
                f32 p = 0, e = 0;
                StringView text = style_vs_engine_zone(s, (StyleZone)z, &p, &e)
                                      ? arena_format(&ui->frame, "%.0f%%  (KataGo %.0f%%, %+.0f)", p * 100, e * 100, (p - e) * 100)
                                      : arena_format(&ui->frame, "%.0f%%", style_zone_share(s, (StyleZone)z) * 100);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8) } }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 64)), CLAY_SIZING_FIT(0) } } }) {
                        ui_label(ui, tr(ui, KEYS[z], FALLBACK[z]), FONT_UI, 13, c->text_muted);
                    }
                    ui_label(ui, text, FONT_MONO, 13, c->text);
                }
            }
        }
    }
}

// The three style sliders for two players' counts.
static void style_page(DeepReport *d, const StyleCounts *black, const StyleCounts *white) {
    Ui *ui = d->ui;
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
        legend_dot(ui, player_bar_color(c, STONE_BLACK), player_name(ui, STONE_BLACK));
        legend_dot(ui, c->winrate_white, player_name(ui, STONE_WHITE));
    }
    StringView bn = player_name(ui, STONE_BLACK), wn = player_name(ui, STONE_WHITE);
    // Against KataGo in the same positions when the game is analysed: the slider's
    // middle is "as KataGo would play here", each dot how far the player leans (25
    // points reach the end). Without analysis, the raw shares.
    typedef bool (*VsFn)(const StyleCounts *, f32 *, f32 *);
    static const VsFn VS[3] = { style_vs_engine_height, style_vs_engine_contact, style_vs_engine_tenuki };
    static const char *const LEFT_KEYS[3] = { "style.territory", "style.calm", "style.local" };
    static const char *const LEFT[3] = { "Territory", "Calm", "Local" };
    static const char *const RIGHT_KEYS[3] = { "style.influence", "style.fighting", "style.tenuki" };
    static const char *const RIGHT[3] = { "Influence", "Fighting", "Tenuki" };
    static const char *const DETAIL_KEYS[3] = { "style.height_detail", "style.contact_detail", "style.tenuki_detail" };
    static const char *const DETAIL[3] = { "of moves on the 4th line or higher", "of moves touch an opponent stone", "of answers played elsewhere" };
    bool compared = false;
    for (i32 k = 0; k < 3; ++k) {
        f32 bp = 0, be = 0, wp = 0, we = 0;
        bool bv = VS[k](black, &bp, &be), wv = VS[k](white, &wp, &we);
        StringView detail;
        f32 bpos, wpos;
        if (bv || wv) {
            compared = true;
            bpos = bv ? CLAMP(0.5f + (bp - be) * 2.0f, 0.0f, 1.0f) : -1;
            wpos = wv ? CLAMP(0.5f + (wp - we) * 2.0f, 0.0f, 1.0f) : -1;
            detail = arena_format(&ui->frame, SV_FMT " %.0f%% (KataGo %.0f%%, %+.0f)  ·  " SV_FMT " %.0f%% (KataGo %.0f%%, %+.0f)  " SV_FMT, SV_ARG(bn),
                                  bp * 100, be * 100, (bp - be) * 100, SV_ARG(wn), wp * 100, we * 100, (wp - we) * 100,
                                  SV_ARG(tr(ui, DETAIL_KEYS[k], DETAIL[k])));
        } else {
            f32 b = k == 0 ? style_height(black) : k == 1 ? style_contact(black) : style_tenuki(black);
            f32 w = k == 0 ? style_height(white) : k == 1 ? style_contact(white) : style_tenuki(white);
            bool bh = k == 2 ? black->answers > 0 : black->moves > 0, wh = k == 2 ? white->answers > 0 : white->moves > 0;
            bpos = bh ? b : -1, wpos = wh ? w : -1;
            detail = arena_format(&ui->frame, SV_FMT " %.0f%%  ·  " SV_FMT " %.0f%%  " SV_FMT, SV_ARG(bn), fmaxf(b, 0) * 100, SV_ARG(wn), fmaxf(w, 0) * 100,
                                  SV_ARG(tr(ui, DETAIL_KEYS[k], DETAIL[k])));
        }
        style_row(ui, (u32)k, tr(ui, LEFT_KEYS[k], LEFT[k]), tr(ui, RIGHT_KEYS[k], RIGHT[k]), bpos, wpos, detail);
    }
    // Where they play.
    section_title(ui, tr(ui, "style.where", "WHERE THEY PLAY"));
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12) } }) {
        where_card(d, STONE_BLACK, black);
        where_card(d, STONE_WHITE, white);
    }
    ui_label(ui, compared ? tr(ui, "style.note_vs", "Compared with KataGo in the same positions: the middle of each line is what KataGo would play there, so the board's shape does not count as style. A style, not a grade.")
                          : tr(ui, "style.note_raw", "Raw shares of the moves (analyse the game to compare with KataGo in the same positions). A style, not a grade."),
             FONT_UI, 12, c->text_muted);
}

// ---- Profile ----------------------------------------------------------------------------------------

static void profile_header(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    ProfileStatus st = app_profile_status(app);
    bool building = st.phase == PROFILE_SCANNING || st.phase == PROFILE_ANALYSING || st.phase == PROFILE_ADDING;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 2) } }) {
            StringView names = string_sv(&app->config.profile_names);
            ui_label(ui, names.len ? arena_format(&ui->frame, SV_FMT ":  " SV_FMT, SV_ARG(tr(ui, "profile.names", "Your names")), SV_ARG(names))
                                   : tr(ui, "profile.no_names", "Set your player names and games folder in Settings → Profile."),
                     FONT_UI, 13, names.len ? c->text : c->text_muted);
            if (app->profile_ready) {
                const ProfileTotals *t = &app->profile.totals;
                ui_label(ui, arena_format(&ui->frame, "%td %s  ·  %s %d–%d  ·  %s %d–%d%s", app->profile.games.len,
                                          tr(ui, "profile.games", "games").data, tr(ui, "profile.black_record", "Black").data,
                                          t->wins[STONE_BLACK], t->losses[STONE_BLACK], tr(ui, "profile.white_record", "White").data,
                                          t->wins[STONE_WHITE], t->losses[STONE_WHITE],
                                          app->profile.skipped ? arena_format(&ui->frame, "  ·  %d skipped", app->profile.skipped).data : ""),
                         FONT_UI, 12, c->text_muted);
            }
        }
        if (building) {
            if (ui_button(ui, CLAY_ID("ProfileCancel"), tr(ui, "label.cancel", "Cancel"), BUTTON_NORMAL, false, true)) app_profile_cancel(app);
        } else if (ui_button(ui, CLAY_ID("ProfileBuild"), app->profile_ready ? tr(ui, "profile.update", "Update profile") : tr(ui, "profile.build", "Build profile"),
                             BUTTON_PRIMARY, false, app->config.profile_names.len && app->config.profile_folder.len)) {
            app_profile_build(app);
        }
        ui_command_button_ex(ui, SV("view.settings"), SV("@gear"), false, BUTTON_NORMAL, 12);
        ui_help_button(ui, SV("profile"));
    }
    if (building) {
        const char *what = st.phase == PROFILE_SCANNING ? "Looking for your games" : st.phase == PROFILE_ANALYSING ? "Analysing" : "Adding up";
        ui_label(ui, arena_format(&ui->frame, "%s  %d / %d  %s", what, st.done, st.total, st.current), FONT_UI, 12, c->text);
        f32 fraction = st.total > 0 ? (f32)st.done / (f32)st.total : 0.0f;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 6)) } }, .backgroundColor = color_to_clay(c->surface_alt),
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
            if (st.total > 0) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(CLAMP(fraction, 0.001f, 1.0f)), CLAY_SIZING_GROW(0) } },
                       .backgroundColor = color_to_clay(c->accent), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
            }
        }
    } else if (st.phase == PROFILE_FAILED) {
        ui_label(ui, sv(st.error), FONT_UI, 12, c->danger);
    }
}

// Accuracy per game, oldest on the left: one dot per game in the user's colour,
// joined per colour; gridlines every 10 points from the lowest to 100.
static void draw_profile_trend(void *user, Rectangle box) {
    Ui *ui = user;
    const ThemeColors *c = &ui->theme->colors;
    const ProfileGameVec *games = &ui->app->profile.games;
    DrawRectangleRec(box, color_with_alpha(c->surface_alt, 0.6f));
    isize n = games->len;
    if (n == 0) return;
    f32 lo = 100.0f;
    vec_foreach(ProfileGame, g, games) lo = fminf(lo, g->accuracy);
    lo = fmaxf(0.0f, floorf(lo / 10.0f) * 10.0f - 10.0f);
    f32 pad = ui_px(ui, 14);
    Rectangle r = { box.x + pad * 2.2f, box.y + pad, box.width - pad * 3.2f, box.height - pad * 2 };
    for (f32 v = lo; v <= 100.0f; v += 10.0f) {
        f32 y = r.y + r.height * (1.0f - (v - lo) / (100.0f - lo));
        DrawLineEx((Vector2){ r.x, y }, (Vector2){ r.x + r.width, y }, 1.0f, color_with_alpha(c->border, 0.7f));
        font_draw(ui->fonts, FONT_MONO, ui_px(ui, 10), arena_format(&ui->frame, "%.0f", v), (Vector2){ box.x + pad * 0.4f, y - ui_px(ui, 6) },
                  c->text_muted);
    }
    // Games are stored newest first: the x position runs the other way.
    Vector2 last[3] = { 0 };
    bool has_last[3] = { 0 };
    for (isize k = n - 1; k >= 0; --k) {
        const ProfileGame *g = &games->items[k];
        f32 t = n > 1 ? (f32)(n - 1 - k) / (f32)(n - 1) : 0.5f;
        Vector2 p = { r.x + r.width * t, r.y + r.height * (1.0f - (g->accuracy - lo) / (100.0f - lo)) };
        Color col = g->color == STONE_BLACK ? c->winrate_black : c->score_line;
        if (has_last[g->color]) DrawLineEx(last[g->color], p, ui_px(ui, 1.5f), color_with_alpha(col, 0.7f));
        last[g->color] = p, has_last[g->color] = true;
    }
    for (isize k = 0; k < n; ++k) {
        const ProfileGame *g = &games->items[k];
        f32 t = n > 1 ? (f32)(n - 1 - k) / (f32)(n - 1) : 0.5f;
        Vector2 p = { r.x + r.width * t, r.y + r.height * (1.0f - (g->accuracy - lo) / (100.0f - lo)) };
        Color col = g->color == STONE_BLACK ? c->winrate_black : c->score_line;
        DrawCircleV(p, ui_px(ui, 4.5f), col);
        if (g->result == PROFILE_RESULT_LOSS) DrawCircleLinesV(p, ui_px(ui, 6.5f), c->candidate_bad);
    }
}

static void profile_trend(Ui *ui, f32 height) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(height) } }, .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)),
           .custom = { .customData = ui_custom(ui, draw_profile_trend, ui) } }) {}
}

void profile_dialog_body(Ui *ui, void *user);
void profile_dialog_body(Ui *ui, void *user) {
    UNUSED(user);
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    profile_header(ui);
    if (!app->profile_ready) {
        ui_label(ui, tr(ui, "profile.intro", "Your profile adds up the Deep reports of all your games: tell Gifu your player names and where your games are (Settings → Profile), then build it. Games are analysed once and kept."),
                 FONT_UI, 13, c->text_muted);
        return;
    }
    DeepReport *d = arena_new(&ui->frame, DeepReport);
    d->ui = ui;
    d->report = app->profile.totals.report;
    enum { PT_OVERVIEW, PT_DISTRIBUTION, PT_STYLE, PT_TREND, PT_GAMES, PT_COUNT };
    static const char *const KEYS[PT_COUNT] = { "report.tab_overview", "report.tab_distribution", "report.tab_style", "profile.tab_trend", "profile.tab_games" };
    static const char *const FALLBACK[PT_COUNT] = { "Overview", "Distribution", "Style", "Trend", "Games" };
    i32 tab = CLAMP(ui->profile_tab, 0, PT_COUNT - 1);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4), .padding = { 0, 0, 0, ui_u16(ui, 6) } },
           .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
        for (i32 t = 0; t < PT_COUNT; ++t) {
            if (ui_button(ui, CLAY_IDI("ProfileTab", (u32)t), tr(ui, KEYS[t], FALLBACK[t]), BUTTON_GHOST, t == tab, true)) ui->profile_tab = t;
        }
    }
    f32 page_h = fminf(ui_px(ui, 520), (f32)GetScreenHeight() * 0.58f);
    d->page_h = page_h;
    g_profile_mode = true;
    g_editing = false;   // the Profile dialog shows the settings as text
    Clay_ElementId page = CLAY_ID("ProfilePage");
    CLAY({ .id = page, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(page_h) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                   .childGap = ui_u16(ui, 12), .padding = { 0, ui_u16(ui, 12), 0, 0 } },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        switch (tab) {
            case PT_OVERVIEW: overview(d); break;
            case PT_DISTRIBUTION: {
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, player_bar_color(c, STONE_BLACK), player_name(ui, STONE_BLACK));
                    legend_dot(ui, player_bar_color(c, STONE_WHITE), player_name(ui, STONE_WHITE));
                }
                f32 row_px = (page_h - ui_px(ui, 120)) / (f32)(2 * LOSS_BUCKETS);
                block(ui, tr(ui, "report.distribution", "POINTS LOST PER MOVE"), SV(""));
                distribution(d, false, row_px);
                block(ui, tr(ui, "report.distribution_wr", "WIN RATE LOST PER MOVE"), SV(""));
                distribution(d, true, row_px);
            } break;
            case PT_STYLE: style_page(d, &app->profile.totals.style[STONE_BLACK], &app->profile.totals.style[STONE_WHITE]); break;
            case PT_TREND:
                block(ui, tr(ui, "profile.trend", "ACCURACY PER GAME (OLDEST → NEWEST)"), SV(""));
                CLAY({ .layout = { .childGap = ui_u16(ui, 12) } }) {
                    legend_dot(ui, player_bar_color(c, STONE_BLACK), player_name(ui, STONE_BLACK));
                    legend_dot(ui, player_bar_color(c, STONE_WHITE), player_name(ui, STONE_WHITE));
                }
                profile_trend(ui, page_h - ui_px(ui, 90));
                break;
            default:
                for (isize i = 0; i < app->profile.games.len; ++i) {
                    const ProfileGame *g = &app->profile.games.items[i];
                    Clay_ElementId row = CLAY_IDI("ProfileGame", (u32)i);
                    bool over = Clay_PointerOver(row);
                    CLAY({ .id = row, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 10),
                                                  .padding = { ui_u16(ui, 8), ui_u16(ui, 8), ui_u16(ui, 5), ui_u16(ui, 5) },
                                                  .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                           .backgroundColor = color_to_clay(over ? c->hover : color_with_alpha(c->hover, 0.0f)), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 90)), CLAY_SIZING_FIT(0) } } }) {
                            ui_label(ui, g->date.len ? string_sv(&g->date) : SV("—"), FONT_MONO, 12, c->text_muted);
                        }
                        ui_stone_dot(ui, g->color);
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                            ui_label(ui, arena_format(&ui->frame, "vs " SV_FMT, SV_ARG(string_sv(&g->opponent))), FONT_UI, 13, c->text);
                        }
                        ui_label(ui, g->result == PROFILE_RESULT_WIN    ? tr(ui, "profile.won", "Won")
                                     : g->result == PROFILE_RESULT_LOSS ? tr(ui, "profile.lost", "Lost")
                                                                        : SV("—"),
                                 FONT_UI_BOLD, 12, g->result == PROFILE_RESULT_WIN ? c->candidate_good : g->result == PROFILE_RESULT_LOSS ? c->candidate_bad : c->text_muted);
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 70)), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
                            ui_label(ui, arena_format(&ui->frame, "%.1f", g->accuracy), FONT_UI_BOLD, 14, c->text);
                        }
                    }
                    if (ui_clicked(ui, row, true)) {
                        String path = string_from(string_sv(&g->path));
                        ui_close_dialog(ui);
                        app_open_sgf(app, string_sv(&path));
                        string_free(&path);
                        break;   // the list may be gone (not inside a CLAY body here)
                    }
                }
                break;
        }
        ui_scrollbar(ui, page, 200);
    }
    g_profile_mode = false;
}
