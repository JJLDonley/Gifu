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
            if (style_line(m.point, before->width, before->height) >= 4) s->high++;
            if (touches(before, m.point, opponent)) s->contact++;
            if (last_move && last_move->move.color == opponent && !point_is_pass(last_move->move.point)) {
                s->answers++;
                Point q = last_move->move.point;
                if (abs(m.point.x - q.x) > STYLE_LOCAL_DISTANCE || abs(m.point.y - q.y) > STYLE_LOCAL_DISTANCE) s->tenuki++;
            }
        }
        last_move = n;
    }
}

void style_add(StyleCounts *to, const StyleCounts *from) {
    to->moves += from->moves, to->high += from->high, to->contact += from->contact;
    to->answers += from->answers, to->tenuki += from->tenuki;
}

f32 style_height(const StyleCounts *s) { return s->moves ? (f32)s->high / (f32)s->moves : 0.0f; }
f32 style_contact(const StyleCounts *s) { return s->moves ? (f32)s->contact / (f32)s->moves : 0.0f; }
f32 style_tenuki(const StyleCounts *s) { return s->answers ? (f32)s->tenuki / (f32)s->answers : 0.0f; }
