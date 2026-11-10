#include "core/tsumego_frame.h"

#include <string.h>

enum { SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM, SIDE_COUNT };

// Chebyshev distance from (x, y) to the rectangle (0 inside).
static i32 rect_distance(BoardRect r, i32 x, i32 y) {
    i32 dx = x < r.x0 ? r.x0 - x : x > r.x1 ? x - r.x1 : 0;
    i32 dy = y < r.y0 ? r.y0 - y : y > r.y1 ? y - r.y1 : 0;
    return dx > dy ? dx : dy;
}

static bool stones_bounding_box(const Board *b, BoardRect *out) {
    bool any = false;
    for (i32 y = 0; y < b->height; ++y) {
        for (i32 x = 0; x < b->width; ++x) {
            if (b->cells[y * b->width + x] == STONE_EMPTY) continue;
            if (!any) *out = (BoardRect){ (i8)x, (i8)y, (i8)x, (i8)y };
            any = true;
            if (x < out->x0) out->x0 = (i8)x;
            if (x > out->x1) out->x1 = (i8)x;
            if (y < out->y0) out->y0 = (i8)y;
            if (y > out->y1) out->y1 = (i8)y;
        }
    }
    return any;
}

// The attacker surrounds the problem: its stones dominate the two outermost
// lines on the open sides. Ties go to the side with more stones, then White.
static Stone detect_attacker(const Board *b, BoardRect box, const bool open[SIDE_COUNT]) {
    i32 outer[3] = { 0 }, total[3] = { 0 };
    for (i32 y = box.y0; y <= box.y1; ++y) {
        for (i32 x = box.x0; x <= box.x1; ++x) {
            Stone s = (Stone)b->cells[y * b->width + x];
            if (s == STONE_EMPTY) continue;
            total[s]++;
            i32 d = 1 << 20;
            if (open[SIDE_LEFT] && x - box.x0 < d) d = x - box.x0;
            if (open[SIDE_TOP] && y - box.y0 < d) d = y - box.y0;
            if (open[SIDE_RIGHT] && box.x1 - x < d) d = box.x1 - x;
            if (open[SIDE_BOTTOM] && box.y1 - y < d) d = box.y1 - y;
            if (d <= 1) outer[s]++;
        }
    }
    if (outer[STONE_BLACK] != outer[STONE_WHITE]) return outer[STONE_BLACK] > outer[STONE_WHITE] ? STONE_BLACK : STONE_WHITE;
    if (total[STONE_BLACK] != total[STONE_WHITE]) return total[STONE_BLACK] > total[STONE_WHITE] ? STONE_BLACK : STONE_WHITE;
    return STONE_WHITE;
}

static const i32 DX[4] = { -1, 1, 0, 0 }, DY[4] = { 0, 0, -1, 1 };

static i32 same_neighbours(const u8 *owner, i32 w, i32 h, i32 x, i32 y) {
    i32 n = 0;
    for (i32 k = 0; k < 4; ++k) {
        i32 nx = x + DX[k], ny = y + DY[k];
        if (nx >= 0 && ny >= 0 && nx < w && ny < h && owner[ny * w + nx] == owner[y * w + x]) n++;
    }
    return n;
}

// Per colour, every 4-connected region of `owner` except the largest is handed
// to the opposite colour.
static void keep_largest_regions(u8 *owner, i32 w, i32 h) {
    i32 region[BOARD_MAX_POINTS], size[BOARD_MAX_POINTS], stack[BOARD_MAX_POINTS];
    i32 regions = 0;
    for (i32 i = 0; i < w * h; ++i) region[i] = -1;
    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] == STONE_EMPTY || region[i] >= 0) continue;
        i32 top = 0;
        size[regions] = 0;
        region[i] = regions;
        stack[top++] = i;
        while (top > 0) {
            i32 p = stack[--top];
            size[regions]++;
            i32 x = p % w, y = p / w;
            const i32 nbs[4] = { x > 0 ? p - 1 : -1, x < w - 1 ? p + 1 : -1, y > 0 ? p - w : -1, y < h - 1 ? p + w : -1 };
            for (i32 k = 0; k < 4; ++k) {
                i32 q = nbs[k];
                if (q < 0 || region[q] >= 0 || owner[q] != owner[i]) continue;
                region[q] = regions;
                stack[top++] = q;
            }
        }
        regions++;
    }
    i32 largest[3] = { -1, -1, -1 };
    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] == STONE_EMPTY) continue;
        i32 *best = &largest[owner[i]];
        if (*best < 0 || size[region[i]] > size[*best]) *best = region[i];
    }
    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] != STONE_EMPTY && region[i] != largest[owner[i]]) owner[i] = (u8)stone_opponent((Stone)owner[i]);
    }
}

// The nearer `split` outside points are the attacker's wall, the rest the defender's area.
static void frame_owner(const i32 *order, i32 n, i32 split, Stone attacker, i32 w, i32 h, u8 *owner) {
    memset(owner, STONE_EMPTY, (usize)(w * h));   // STONE_EMPTY = problem area
    split = split < 1 ? 1 : split > n - 1 ? n - 1 : split;
    for (i32 k = 0; k < n; ++k) owner[order[k]] = (u8)(k < split ? attacker : stone_opponent(attacker));
    // The split runs through one distance ring and can leave fragments cut off
    // from their colour's main area: give them to the other side.
    for (i32 pass = 0; pass < 2; ++pass) keep_largest_regions(owner, w, h);
}

// Eyes: isolated holes ((x + 2y) mod 5 never repeats between neighbours),
// only where every neighbour is the same colour's stone that stays
// connected without the hole (two other same-colour neighbours).
static void frame_stones(const u8 *owner, i32 w, i32 h, u8 *want) {
    for (i32 i = 0; i < w * h; ++i) {
        want[i] = STONE_EMPTY;
        if (owner[i] == STONE_EMPTY) continue;
        i32 x = i % w, y = i / w;
        bool hole = (x + 2 * y) % 5 == 0;
        for (i32 k = 0; k < 4 && hole; ++k) {
            i32 nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            hole = owner[ny * w + nx] == owner[i] && same_neighbours(owner, w, h, nx, ny) >= 3;
        }
        want[i] = hole ? STONE_EMPTY : owner[i];
    }
}

// ---- Ko threats -----------------------------------------------------------------------------------

static bool on_board(i32 w, i32 h, i32 x, i32 y) { return x >= 0 && y >= 0 && x < w && y < h; }

// Every chain of frame stones (outside the problem area) except those holding a `skip` cell has
// two eyes: empty points all of whose neighbours belong to that one chain.
static bool frame_alive(const u8 *want, const u8 *owner, i32 w, i32 h, const i32 *skip, i32 skip_count) {
    i32 chain[BOARD_MAX_POINTS], stack[BOARD_MAX_POINTS], eyes[BOARD_MAX_POINTS];
    i32 chains = 0;
    for (i32 i = 0; i < w * h; ++i) chain[i] = -1;
    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] == STONE_EMPTY || want[i] == STONE_EMPTY || chain[i] >= 0) continue;
        i32 top = 0;
        chain[i] = chains;
        stack[top++] = i;
        while (top > 0) {
            i32 p = stack[--top], x = p % w, y = p / w;
            for (i32 k = 0; k < 4; ++k) {
                i32 nx = x + DX[k], ny = y + DY[k], q = ny * w + nx;
                if (!on_board(w, h, nx, ny) || owner[q] == STONE_EMPTY || chain[q] >= 0 || want[q] != want[i]) continue;
                chain[q] = chains;
                stack[top++] = q;
            }
        }
        eyes[chains++] = 0;
    }
    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] == STONE_EMPTY || want[i] != STONE_EMPTY) continue;
        i32 x = i % w, y = i / w, c = -2;
        for (i32 k = 0; k < 4 && c != -1; ++k) {
            i32 nx = x + DX[k], ny = y + DY[k];
            if (!on_board(w, h, nx, ny)) continue;
            i32 nc = chain[ny * w + nx];
            c = nc < 0 || (c >= 0 && nc != c) ? -1 : nc;
        }
        if (c >= 0) eyes[c]++;
    }
    for (i32 c = 0; c < chains; ++c) {
        if (eyes[c] >= 2) continue;
        bool block = false;   // threat blocks have no eyes by design
        for (i32 k = 0; k < skip_count && !block; ++k) block = chain[skip[k]] == c;
        if (!block) return false;
    }
    return true;
}

// Candidate: a block of the opponent's stones (a rectangle plus a one-stone
// tail next to it), the answer point next to the tail, the move next to the block.
typedef struct ThreatShape {
    i32 cells[24];
    i32 count;
    i32 move, answer;
} ThreatShape;
static bool in_list(const i32 *list, i32 n, i32 v) {
    for (i32 i = 0; i < n; ++i) {
        if (list[i] == v) return true;
    }
    return false;
}

// Tries to build the shape for `side` into `want`; keeps it if every frame
// group except the block still has two eyes.
static bool try_threat(u8 *want, const u8 *owner, bool *locked, i32 *blocks, i32 *block_count, i32 w, i32 h, BoardRect area, Stone side,
                       const ThreatShape *s) {
    Stone other = stone_opponent(side);
    i32 touched[96], n = 0;   // cells the shape sets, with their new colour
    u8 colour[96];
    // The block, the move and the answer: side's area, away from the problem.
    for (i32 i = 0; i <= s->count + 1; ++i) {
        i32 c = i < s->count ? s->cells[i] : i == s->count ? s->move : s->answer;
        if (locked[c] || (c != s->move && rect_distance(area, c % w, c / w) < 2)) return false;
        if (c != s->answer && owner[c] != side) return false;
        touched[n] = c, colour[n++] = i < s->count ? (u8)other : STONE_EMPTY;
    }
    // Around the block: side's stones (the block's only liberties are move and answer).
    for (i32 i = 0; i < s->count + 2; ++i) {
        i32 c = i < s->count ? s->cells[i] : i == s->count ? s->move : s->answer;
        Stone around = c == s->answer ? other : side;   // the answer sits among the opponent's stones
        i32 x = c % w, y = c / w, nb = 0;
        for (i32 k = 0; k < 4; ++k) {
            i32 nx = x + DX[k], ny = y + DY[k], q = ny * w + nx;
            if (!on_board(w, h, nx, ny) || in_list(s->cells, s->count, q)) continue;
            if (q == s->move || q == s->answer) {
                if (i >= s->count) return false;   // move and answer never touch
                continue;                          // the block's two liberties
            }
            if (owner[q] != around || locked[q]) return false;
            if (n >= (i32)countof(touched)) return false;
            touched[n] = q, colour[n++] = (u8)around;
            nb++;
        }
        if (c != s->answer && i >= s->count && nb == 0) return false;   // the move must join side's stones
        if (c == s->answer && nb == 0) return false;
    }
    u8 saved[96];
    for (i32 i = 0; i < n; ++i) saved[i] = want[touched[i]];
    for (i32 i = 0; i < n; ++i) want[touched[i]] = colour[i];
    blocks[*block_count] = s->cells[0];
    if (!frame_alive(want, owner, w, h, blocks, *block_count + 1)) {
        for (i32 i = n - 1; i >= 0; --i) want[touched[i]] = saved[i];
        return false;
    }
    for (i32 i = 0; i < n; ++i) locked[touched[i]] = true;
    (*block_count)++;
    return true;
}

// Points next to the shape's cells (not in it), in a fixed order.
static i32 perimeter(const ThreatShape *s, i32 w, i32 h, i32 *out) {
    i32 n = 0;
    for (i32 i = 0; i < s->count; ++i) {
        i32 x = s->cells[i] % w, y = s->cells[i] / w;
        for (i32 k = 0; k < 4; ++k) {
            i32 nx = x + DX[k], ny = y + DY[k], q = ny * w + nx;
            if (on_board(w, h, nx, ny) && !in_list(s->cells, s->count, q) && !in_list(out, n, q)) out[n++] = q;
        }
    }
    return n;
}

static bool cells_free(const u8 *owner, const bool *locked, i32 w, BoardRect area, Stone side, const ThreatShape *s) {
    for (i32 i = 0; i < s->count; ++i) {
        i32 c = s->cells[i];
        if (owner[c] != side || locked[c] || rect_distance(area, c % w, c / w) < 2) return false;
    }
    return true;
}

static void place_ko_threat(u8 *want, const u8 *owner, bool *locked, i32 *blocks, i32 *block_count, i32 w, i32 h, BoardRect area, Stone side,
                            TsumegoKoThreat *out) {
    // Biggest first: the threat has to outweigh the ko it is used in.
    static const struct { i32 depth, len; } SIZES[] = { { 2, 8 }, { 2, 6 }, { 1, 9 }, { 2, 4 }, { 1, 6 }, { 1, 4 } };
    for (i32 z = 0; z < (i32)countof(SIZES); ++z) {
        // Farthest from the problem first.
        for (i32 d = BOARD_MAX_SIZE; d >= 2; --d) {
            for (i32 i = 0; i < w * h; ++i) {
                if (rect_distance(area, i % w, i / w) != d) continue;
                for (i32 vertical = 0; vertical < 2; ++vertical) {
                    i32 cw = vertical ? SIZES[z].depth : SIZES[z].len, ch = vertical ? SIZES[z].len : SIZES[z].depth;
                    i32 x0 = i % w, y0 = i / w;
                    if (x0 + cw > w || y0 + ch > h) continue;
                    ThreatShape s = { .count = 0 };
                    for (i32 y = y0; y < y0 + ch; ++y) {
                        for (i32 x = x0; x < x0 + cw; ++x) s.cells[s.count++] = y * w + x;
                    }
                    if (!cells_free(owner, locked, w, area, side, &s)) continue;
                    i32 tails[64];
                    i32 tail_count = perimeter(&s, w, h, tails);
                    for (i32 t = 0; t < tail_count; ++t) {
                        ThreatShape full = s;
                        full.cells[full.count++] = tails[t];
                        if (!cells_free(owner, locked, w, area, side, &full)) continue;
                        i32 tx = tails[t] % w, ty = tails[t] / w;
                        for (i32 k = 0; k < 4; ++k) {
                            i32 ax = tx + DX[k], ay = ty + DY[k];
                            if (!on_board(w, h, ax, ay) || in_list(full.cells, full.count, ay * w + ax)) continue;
                            full.answer = ay * w + ax;
                            i32 around[64];
                            i32 around_count = perimeter(&full, w, h, around);
                            for (i32 m = 0; m < around_count; ++m) {
                                full.move = around[m];
                                if (full.move == full.answer || !try_threat(want, owner, locked, blocks, block_count, w, h, area, side, &full)) continue;
                                *out = (TsumegoKoThreat){ true, { (i8)(full.move % w), (i8)(full.move / w) },
                                                          { (i8)(full.answer % w), (i8)(full.answer / w) }, full.count };
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
}

bool tsumego_frame_build(const Board *b, const TsumegoFrameOptions *options, TsumegoFrame *out, const char **error) {
    const char *dummy;
    if (!error) error = &dummy;
    memset(out, 0, sizeof(*out));
    const i32 w = b->width, h = b->height;
    const i32 margin = options->margin >= 0 ? options->margin : 2;

    BoardRect box;
    if (options->has_problem) {
        box = options->problem;
        if (box.x0 < 0) box.x0 = 0;
        if (box.y0 < 0) box.y0 = 0;
        if (box.x1 > w - 1) box.x1 = (i8)(w - 1);
        if (box.y1 > h - 1) box.y1 = (i8)(h - 1);
        if (box.x0 > box.x1 || box.y0 > box.y1) {
            *error = "the problem area is outside the board";
            return false;
        }
    } else if (!stones_bounding_box(b, &box)) {
        *error = "the board is empty";
        return false;
    }

    // A side closer to the edge than a wall needs is extended to the edge.
    const i32 snap = margin + 2;
    bool open[SIDE_COUNT] = {
        [SIDE_LEFT] = box.x0 >= snap,
        [SIDE_TOP] = box.y0 >= snap,
        [SIDE_RIGHT] = w - 1 - box.x1 >= snap,
        [SIDE_BOTTOM] = h - 1 - box.y1 >= snap,
    };
    if (!open[SIDE_LEFT]) box.x0 = 0;
    if (!open[SIDE_TOP]) box.y0 = 0;
    if (!open[SIDE_RIGHT]) box.x1 = (i8)(w - 1);
    if (!open[SIDE_BOTTOM]) box.y1 = (i8)(h - 1);
    if (!open[SIDE_LEFT] && !open[SIDE_TOP] && !open[SIDE_RIGHT] && !open[SIDE_BOTTOM]) {
        *error = "the problem covers the whole board";
        return false;
    }
    BoardRect area = {
        (i8)(open[SIDE_LEFT] ? box.x0 - margin : 0),
        (i8)(open[SIDE_TOP] ? box.y0 - margin : 0),
        (i8)(open[SIDE_RIGHT] ? box.x1 + margin : w - 1),
        (i8)(open[SIDE_BOTTOM] ? box.y1 + margin : h - 1),
    };
    Stone attacker = options->attacker != STONE_EMPTY ? options->attacker : detect_attacker(b, box, open);
    Stone defender = stone_opponent(attacker);

    // Outside points, nearest to the problem first (bucket sort by distance).
    i32 order[BOARD_MAX_POINTS];
    i32 n = 0;
    for (i32 d = 1; d < BOARD_MAX_SIZE; ++d) {
        for (i32 i = 0; i < w * h; ++i) {
            if (rect_distance(area, i % w, i / w) == d) order[n++] = i;
        }
    }
    if (n < 16) {
        *error = "not enough room around the problem for a frame";
        return false;
    }

    // A ko threat for one side puts a block of the other side's stones into its
    // area, which the other side keeps once the threat is answered. Moving the
    // wall/area split by the difference keeps the frame's score balanced; the
    // threats are placed again on the new split until it stops changing.
    u8 owner[BOARD_MAX_POINTS], want[BOARD_MAX_POINTS];
    i32 shift = 0;
    for (i32 attempt = 0; attempt < 4; ++attempt) {
        frame_owner(order, n, (n + 1) / 2 + shift, attacker, w, h, owner);
        frame_stones(owner, w, h, want);
        memset(out->ko_threat, 0, sizeof(out->ko_threat));
        bool locked[BOARD_MAX_POINTS] = { 0 };
        i32 blocks[2], block_count = 0;
        const Stone sides[2] = { attacker, defender };   // the attacker's first: its wall is the scarcer room
        for (i32 k = 0; k < 2; ++k) {
            if (!(k == 0 ? options->ko_threat_attacker : options->ko_threat_defender)) continue;
            place_ko_threat(want, owner, locked, blocks, &block_count, w, h, area, sides[k], &out->ko_threat[sides[k]]);
        }
        const TsumegoKoThreat *ta = &out->ko_threat[attacker], *td = &out->ko_threat[defender];
        i32 balanced = (ta->placed ? ta->stones : 0) - (td->placed ? td->stones : 0);
        if (balanced == shift) break;
        shift = balanced;
    }

    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] == STONE_EMPTY || b->cells[i] == want[i]) continue;
        out->changes[out->count++] = (Move){ (Stone)want[i], { (i8)(i % w), (i8)(i / w) } };
    }
    out->attacker = attacker;
    out->area = area;
    return true;
}
