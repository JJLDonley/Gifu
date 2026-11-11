#include "app/ai_style.h"

#include "core/style.h"

AiStyle ai_style_from(StringView id) {
    if (sv_eq(id, SV("human"))) return AI_STYLE_HUMAN;
    if (sv_eq(id, SV("territorial"))) return AI_STYLE_TERRITORIAL;
    if (sv_eq(id, SV("influence"))) return AI_STYLE_INFLUENCE;
    if (sv_eq(id, SV("fighting"))) return AI_STYLE_FIGHTING;
    if (sv_eq(id, SV("calm"))) return AI_STYLE_CALM;
    return AI_STYLE_STRONGEST;
}

static i32 contacts(const Board *b, Point p, Stone opponent) {
    static const i8 D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    i32 n = 0;
    for (i32 k = 0; k < 4; ++k) {
        Point q = { (i8)(p.x + D[k][0]), (i8)(p.y + D[k][1]) };
        if (board_contains(b, q) && board_at(b, q) == opponent) ++n;
    }
    return n;
}

// How well a move fits the style (higher is better).
static i32 fit(AiStyle style, Point p, const Board *b, Stone opponent) {
    if (point_is_pass(p)) return -100;
    i32 line = style_line(p, b->width, b->height);
    switch (style) {
        case AI_STYLE_TERRITORIAL: return line == 3 ? 3 : line == 2 ? 2 : line == 4 ? 1 : line == 1 ? -1 : 0;
        case AI_STYLE_INFLUENCE: return line >= 4 ? MIN(line, 7) : 0;
        case AI_STYLE_FIGHTING: return contacts(b, p, opponent);
        case AI_STYLE_CALM: return -contacts(b, p, opponent);
        default: return 0;
    }
}

i32 ai_style_pick(AiStyle style, const AnalysisSnapshot *a, const Board *board) {
    if (!a || a->candidate_count == 0) return -1;
    const Candidate *best = &a->candidates[0];
    Stone opponent = a->to_play == STONE_BLACK ? STONE_WHITE : STONE_BLACK;
    i32 pick = 0, pick_fit = fit(style, best->move, board, opponent);
    for (i32 i = 1; i < a->candidate_count; ++i) {
        const Candidate *c = &a->candidates[i];
        if (best->score_lead - c->score_lead > AI_STYLE_BUDGET) continue;            // gives up too much
        if ((i64)c->visits * 1000 < (i64)best->visits * AI_STYLE_MIN_VISIT_PERMILLE) continue;   // barely searched
        i32 f = fit(style, c->move, board, opponent);
        if (f > pick_fit) pick = i, pick_fit = f;
    }
    return pick;
}
