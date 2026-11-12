#include "core/style.h"

#include <stdlib.h>

i32 style_line(Point p, i32 w, i32 h) {
    i32 a = MIN(p.x, w - 1 - p.x), b = MIN(p.y, h - 1 - p.y);
    return MIN(a, b) + 1;
}

static bool touches(const Board *b, Point p, Stone opponent) {
    static const i8 D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (i32 k = 0; k < 4; ++k) {
        Point q = { (i8)(p.x + D[k][0]), (i8)(p.y + D[k][1]) };
        if (board_contains(b, q) && board_at(b, q) == opponent) return true;
    }
    return false;
}

// More than STYLE_LOCAL_DISTANCE points away from q in either direction.
static bool far(Point p, Point q) { return abs(p.x - q.x) > STYLE_LOCAL_DISTANCE || abs(p.y - q.y) > STYLE_LOCAL_DISTANCE; }

void style_compute(GameNode *const *line, isize count, StyleReport *out) {
    *out = (StyleReport){ 0 };
    const GameNode *last_move = NULL;   // the previous node with a move
    for (isize i = 0; i < count; ++i) {
        const GameNode *n = line[i];
        if (!n->has_move) continue;
        Move m = n->move;
        if ((m.color == STONE_BLACK || m.color == STONE_WHITE) && !point_is_pass(m.point) && n->parent) {
            StyleCounts *s = &out->player[m.color];
            const Board *before = &n->parent->board;
            Stone opponent = m.color == STONE_BLACK ? STONE_WHITE : STONE_BLACK;
            s->moves++;
            bool high = style_line(m.point, before->width, before->height) >= 4, contact = touches(before, m.point, opponent);
            if (high) s->high++;
            if (contact) s->contact++;
            bool answer = last_move && last_move->move.color == opponent && !point_is_pass(last_move->move.point);
            Point q = answer ? last_move->move.point : POINT_PASS;
            bool tenuki = answer && far(m.point, q);
            if (answer) s->answers++;
            if (tenuki) s->tenuki++;
            // KataGo's choice here: its candidates weighted by their share of the search.
            const NodeEval *e = &n->parent->eval;
            f32 total = 0, eh = 0, ec = 0, et = 0;
            for (i32 k = 0; e->valid && k < e->top_count; ++k) {
                Point c = e->top[k];
                f32 w = e->top_share[k];
                if (w <= 0 || point_is_pass(c) || !board_contains(before, c) || board_at(before, c) != STONE_EMPTY) continue;
                total += w;
                if (style_line(c, before->width, before->height) >= 4) eh += w;
                if (touches(before, c, opponent)) ec += w;
                if (answer && far(c, q)) et += w;
            }
            if (total > 0) {
                s->base_moves++;
                s->base_high += high, s->base_contact += contact;
                s->exp_high += eh / total, s->exp_contact += ec / total;
                if (answer) {
                    s->base_answers++;
                    s->base_tenuki += tenuki;
                    s->exp_tenuki += et / total;
                }
            }
        }
        last_move = n;
    }
}

void style_add(StyleCounts *to, const StyleCounts *from) {
    to->moves += from->moves, to->high += from->high, to->contact += from->contact;
    to->answers += from->answers, to->tenuki += from->tenuki;
    to->base_moves += from->base_moves, to->base_high += from->base_high, to->base_contact += from->base_contact;
    to->exp_high += from->exp_high, to->exp_contact += from->exp_contact;
    to->base_answers += from->base_answers, to->base_tenuki += from->base_tenuki, to->exp_tenuki += from->exp_tenuki;
}

static bool vs(i32 n, i32 own, f32 exp, f32 *player, f32 *engine) {
    if (n <= 0) return false;
    *player = (f32)own / (f32)n, *engine = exp / (f32)n;
    return true;
}
bool style_vs_engine_height(const StyleCounts *s, f32 *p, f32 *e) { return vs(s->base_moves, s->base_high, s->exp_high, p, e); }
bool style_vs_engine_contact(const StyleCounts *s, f32 *p, f32 *e) { return vs(s->base_moves, s->base_contact, s->exp_contact, p, e); }
bool style_vs_engine_tenuki(const StyleCounts *s, f32 *p, f32 *e) { return vs(s->base_answers, s->base_tenuki, s->exp_tenuki, p, e); }

f32 style_height(const StyleCounts *s) { return s->moves ? (f32)s->high / (f32)s->moves : 0.0f; }
f32 style_contact(const StyleCounts *s) { return s->moves ? (f32)s->contact / (f32)s->moves : 0.0f; }
f32 style_tenuki(const StyleCounts *s) { return s->answers ? (f32)s->tenuki / (f32)s->answers : 0.0f; }
