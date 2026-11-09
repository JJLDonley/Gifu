#include "core/board.h"

#include <string.h>
#include "base/thread.h"

// ---- Zobrist -----------------------------------------------------------------

static u64 g_zobrist[3][BOARD_MAX_POINTS];
static OnceFlag g_zobrist_once = ONCE_FLAG_INITIALIZER;

static u64 splitmix64(u64 *state) {
    u64 z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void zobrist_init(void) {
    u64 state = 0xC47A60C47A60ull;
    for (i32 c = 0; c < 3; ++c) {
        for (i32 i = 0; i < BOARD_MAX_POINTS; ++i) g_zobrist[c][i] = c == 0 ? 0 : splitmix64(&state);
    }
}

// ---- Helpers -----------------------------------------------------------------

typedef struct Neighbors {
    i32 index[4];
    i32 count;
} Neighbors;

static Neighbors neighbors_of(const Board *b, i32 index) {
    Neighbors n = { 0 };
    i32 x = index % b->width, y = index / b->width;
    if (x > 0) n.index[n.count++] = index - 1;
    if (x < b->width - 1) n.index[n.count++] = index + 1;
    if (y > 0) n.index[n.count++] = index - b->width;
    if (y < b->height - 1) n.index[n.count++] = index + b->width;
    return n;
}

// Flood-fills the group at `start`. Returns group size; *liberties gets the
// number of distinct liberties. `members` may be NULL.
static i32 flood_group(const Board *b, i32 start, i32 *members, i32 capacity, i32 *liberties) {
    u8 visited[BOARD_MAX_POINTS] = { 0 };
    u8 liberty_seen[BOARD_MAX_POINTS] = { 0 };
    i32 stack[BOARD_MAX_POINTS];
    i32 top = 0, size = 0, libs = 0;
    u8 color = b->cells[start];
    stack[top++] = start;
    visited[start] = 1;
    while (top > 0) {
        i32 cur = stack[--top];
        if (members && size < capacity) members[size] = cur;
        size++;
        Neighbors n = neighbors_of(b, cur);
        for (i32 k = 0; k < n.count; ++k) {
            i32 nb = n.index[k];
            if (b->cells[nb] == STONE_EMPTY) {
                if (!liberty_seen[nb]) {
                    liberty_seen[nb] = 1;
                    libs++;
                }
            } else if (b->cells[nb] == color && !visited[nb]) {
                visited[nb] = 1;
                stack[top++] = nb;
            }
        }
    }
    if (liberties) *liberties = libs;
    return size;
}

static i32 remove_group(Board *b, i32 start) {
    i32 members[BOARD_MAX_POINTS];
    i32 size = flood_group(b, start, members, BOARD_MAX_POINTS, NULL);
    for (i32 i = 0; i < size; ++i) {
        b->hash ^= g_zobrist[b->cells[members[i]]][members[i]];
        b->cells[members[i]] = STONE_EMPTY;
    }
    return size;
}

// ---- Public API ----------------------------------------------------------------

void board_init(Board *board, i32 width, i32 height) {
    run_once(&g_zobrist_once, zobrist_init);
    memset(board, 0, sizeof(*board));
    board->width = CLAMP(width, 2, BOARD_MAX_SIZE);
    board->height = CLAMP(height, 2, BOARD_MAX_SIZE);
    board->ko_index = -1;
}

void board_set(Board *board, Point p, Stone stone) {
    if (!board_contains(board, p)) return;
    i32 i = board_index(board, p);
    board->hash ^= g_zobrist[board->cells[i]][i];
    board->cells[i] = (u8)stone;
    board->hash ^= g_zobrist[stone][i];
    board->ko_index = -1;
}

MoveError board_check(const Board *board, Move move, BoardRules rules) {
    Board copy = *board;
    return board_play(&copy, move, rules);
}

MoveError board_play(Board *board, Move move, BoardRules rules) {
    if (point_is_pass(move.point)) {
        board->ko_index = -1;
        return MOVE_OK;
    }
    if (!board_contains(board, move.point)) return MOVE_ERR_OUT_OF_BOUNDS;
    i32 index = board_index(board, move.point);
    if (board->cells[index] != STONE_EMPTY) return MOVE_ERR_OCCUPIED;
    if (index == board->ko_index && !rules.ignore_ko) return MOVE_ERR_KO;

    Board saved = *board;
    Stone opponent = stone_opponent(move.color);
    board->cells[index] = (u8)move.color;
    board->hash ^= g_zobrist[move.color][index];

    i32 captured = 0, last_captured = -1;
    Neighbors n = neighbors_of(board, index);
    for (i32 k = 0; k < n.count; ++k) {
        i32 nb = n.index[k];
        if (board->cells[nb] != opponent) continue;
        i32 libs = 0;
        flood_group(board, nb, NULL, 0, &libs);
        if (libs == 0) {
            captured += remove_group(board, nb);
            last_captured = nb;
        }
    }

    i32 own_libs = 0;
    i32 own_size = flood_group(board, index, NULL, 0, &own_libs);
    if (own_libs == 0) {
        if (!rules.allow_suicide) {
            *board = saved;
            return MOVE_ERR_SUICIDE;
        }
        board->captures[opponent] += remove_group(board, index);
    }

    board->captures[move.color] += captured;
    board->ko_index = (captured == 1 && own_size == 1 && own_libs == 1) ? last_captured : -1;
    return MOVE_OK;
}

i32 board_liberties(const Board *board, Point p) {
    if (!board_contains(board, p) || board_at(board, p) == STONE_EMPTY) return 0;
    i32 libs = 0;
    flood_group(board, board_index(board, p), NULL, 0, &libs);
    return libs;
}

i32 board_group(const Board *board, Point p, i32 *out, i32 capacity) {
    if (!board_contains(board, p) || board_at(board, p) == STONE_EMPTY) return 0;
    return flood_group(board, board_index(board, p), out, capacity, NULL);
}

i32 board_stone_count(const Board *board, Stone stone) {
    i32 count = 0;
    for (i32 i = 0; i < board->width * board->height; ++i) count += board->cells[i] == stone;
    return count;
}

bool board_is_star_point(i32 size, i32 x, i32 y) {
    if (size < 7) return false;
    i32 edge = size >= 13 ? 3 : 2;
    i32 mid = size / 2;
    bool odd = size % 2 == 1;
    bool xs = x == edge || x == size - 1 - edge || (odd && size >= 13 && x == mid);
    bool ys = y == edge || y == size - 1 - edge || (odd && size >= 13 && y == mid);
    if (size < 13 && odd && x == mid && y == mid) return true;
    return xs && ys;
}

i32 board_handicap_points(i32 width, i32 height, i32 count, Point *out) {
    if (width != height || width < 7 || count < 2) return 0;
    i32 n = width;
    i32 e = n >= 13 ? 3 : 2;
    i32 far = n - 1 - e, mid = n / 2;
    bool odd = n % 2 == 1;
    i32 max = odd && n >= 9 ? 9 : 4;
    count = MIN(count, max);
    Point tr = { (i8)far, (i8)e }, bl = { (i8)e, (i8)far }, br = { (i8)far, (i8)far }, tl = { (i8)e, (i8)e };
    Point left = { (i8)e, (i8)mid }, right = { (i8)far, (i8)mid }, top = { (i8)mid, (i8)e }, bottom = { (i8)mid, (i8)far };
    Point center = { (i8)mid, (i8)mid };
    i32 k = 0;
    out[k++] = tr;
    out[k++] = bl;
    if (count >= 3) out[k++] = br;
    if (count >= 4) out[k++] = tl;
    if (count == 5 || count == 7 || count == 9) out[k++] = center;
    if (count >= 6) out[k++] = left, out[k++] = right;
    if (count >= 8) out[k++] = top, out[k++] = bottom;
    return k;
}

const char *move_error_name(MoveError err) {
    switch (err) {
        case MOVE_OK: return "ok";
        case MOVE_ERR_OUT_OF_BOUNDS: return "out of bounds";
        case MOVE_ERR_OCCUPIED: return "occupied";
        case MOVE_ERR_KO: return "ko";
        case MOVE_ERR_SUICIDE: return "suicide";
    }
    return "?";
}
