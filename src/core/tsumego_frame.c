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

    // The nearer half is the attacker's wall, the rest the defender's area.
    u8 owner[BOARD_MAX_POINTS] = { 0 };   // STONE_EMPTY = problem area
    for (i32 k = 0; k < n; ++k) owner[order[k]] = (u8)(k < (n + 1) / 2 ? attacker : defender);

    // The split runs through one distance ring and can leave fragments cut off
    // from their colour's main area: give them to the other side.
    for (i32 pass = 0; pass < 2; ++pass) keep_largest_regions(owner, w, h);

    // Eyes: isolated holes ((x + 2y) mod 5 never repeats between neighbours),
    // only where every neighbour is the same colour's stone that stays
    // connected without the hole (two other same-colour neighbours).
    for (i32 i = 0; i < w * h; ++i) {
        if (owner[i] == STONE_EMPTY) continue;
        i32 x = i % w, y = i / w;
        bool hole = (x + 2 * y) % 5 == 0;
        for (i32 k = 0; k < 4 && hole; ++k) {
            i32 nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            hole = owner[ny * w + nx] == owner[i] && same_neighbours(owner, w, h, nx, ny) >= 3;
        }
        Stone want = hole ? STONE_EMPTY : (Stone)owner[i];
        if (b->cells[i] != want) out->changes[out->count++] = (Move){ want, { (i8)x, (i8)y } };
    }
    out->attacker = attacker;
    out->area = area;
    return true;
}
