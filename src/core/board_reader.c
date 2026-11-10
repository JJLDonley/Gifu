#include "core/board_reader.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static f32 luma(const u8 *p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

// ---- Grid -------------------------------------------------------------------------------------------

enum { LINE_REACH = 3 };   // a line pixel is darker than both pixels this far away

enum { MIN_RUN = 10 };   // grid lines run on between intersections; stone outlines touch a column only briefly

// profile[i] = share of pixels along line i (column for vertical lines) that lie on
// a thin line: darker (light: brighter, for dark boards) than both pixels LINE_REACH
// away across it, in a run of at least MIN_RUN such pixels along it.
static void line_profile(const RgbaImage *im, const f32 *lum, bool vertical, bool light, f32 *profile) {
    i32 w = im->width, h = im->height, n = vertical ? w : h, along = vertical ? h : w;
    for (i32 i = 0; i < n; ++i) {
        i32 hits = 0, run = 0;
        if (i >= LINE_REACH && i < n - LINE_REACH) {
            for (i32 j = 0; j <= along; ++j) {
                bool line = false;
                if (j < along) {
                    i32 x = vertical ? i : j, y = vertical ? j : i;
                    i32 dx = vertical ? LINE_REACH : 0, dy = vertical ? 0 : LINE_REACH;
                    f32 c = lum[y * w + x], a = lum[(y - dy) * w + (x - dx)], b = lum[(y + dy) * w + (x + dx)];
                    line = light ? c - fmaxf(a, b) > 25.0f : fminf(a, b) - c > 25.0f;
                }
                if (line) {
                    run++;
                } else {
                    if (run >= MIN_RUN) hits += run;
                    run = 0;
                }
            }
        }
        profile[i] = (f32)hits / (f32)along;
    }
}

// Profile value near position p: max within +-radius pixels (lines are 1-3 px, positions fractional).
static f32 at(const f32 *profile, i32 n, f32 p, f32 radius) {
    f32 best = 0;
    for (i32 k = (i32)floorf(p - radius); k <= (i32)ceilf(p + radius); ++k) {
        if (k >= 0 && k < n) best = fmaxf(best, profile[k]);
    }
    return best;
}

// Best N-line fit along one axis. Returns the score (lines minus midpoints / outside).
static f32 fit_axis(const f32 *profile, i32 n, i32 lines, f32 *origin, f32 *step) {
    f32 best = -1.0f;
    f32 max_step = (f32)(n - 1) / (f32)(lines - 1), min_step = fmaxf(max_step * 0.5f, 4.0f);
    for (f32 d = min_step; d <= max_step; d += 0.25f) {   // coarse: refine_axis makes it exact
        f32 span = d * (f32)(lines - 1);
        for (f32 o = 0; o + span <= (f32)(n - 1); o += 1.0f) {
            f32 on = 0, off = 0;
            f32 tol = fmaxf(1.0f, 0.04f * d);   // the coarse search can be a couple of pixels off at the far end
            for (i32 k = 0; k < lines; ++k) on += at(profile, n, o + (f32)k * d, tol);
            for (i32 k = 0; k < lines - 1; ++k) off += at(profile, n, o + ((f32)k + 0.5f) * d, tol);
            // Another line a step beyond either end means the grid is bigger than this fit.
            // (Wide window: a fit that is a pixel off would otherwise slip past the next line.)
            f32 slack = fmaxf(2.0f, 0.15f * d);
            f32 before = o - d >= -slack ? at(profile, n, o - d, slack) : 0, after = o + span + d <= (f32)n - 1 + slack ? at(profile, n, o + span + d, slack) : 0;
            f32 score = on / (f32)lines - off / (f32)(lines - 1) - fmaxf(before, after);
            if (score > best) best = score, *origin = o, *step = d;
        }
    }
    return best;
}

// Sub-pixel fit: each line's peak (profile centroid near the predicted place),
// then origin and step by weighted least squares.
static void refine_axis(const f32 *profile, i32 n, i32 lines, f32 *origin, f32 *step) {
    f64 sw = 0, sk = 0, sp = 0, skk = 0, skp = 0;
    for (i32 k = 0; k < lines; ++k) {
        f32 predicted = *origin + (f32)k * *step;
        i32 best = -1;
        for (i32 q = (i32)floorf(predicted - 0.2f * *step); q <= (i32)ceilf(predicted + 0.2f * *step); ++q) {
            if (q >= 0 && q < n && (best < 0 || profile[q] > profile[best])) best = q;
        }
        if (best < 0 || profile[best] <= 0.02f) continue;
        f64 wsum = 0, psum = 0;
        for (i32 q = best - 1; q <= best + 1; ++q) {
            if (q >= 0 && q < n) wsum += profile[q], psum += profile[q] * q;
        }
        f64 pos = psum / wsum, wt = profile[best];
        sw += wt, sk += wt * k, sp += wt * pos, skk += wt * k * k, skp += wt * k * pos;
    }
    f64 det = sw * skk - sk * sk;
    if (sw <= 0 || fabs(det) < 1e-9) return;
    f64 d = (sw * skp - sk * sp) / det, o = (sp - d * sk) / sw;
    if (fabs(d - *step) < 0.1 * *step) *step = (f32)d, *origin = (f32)o;
}

bool board_grid_detect(const RgbaImage *im, i32 size, BoardGrid *out) {
    i32 w = im->width, h = im->height;
    if (w < 40 || h < 40) return false;
    f32 *lum = malloc(sizeof(f32) * (usize)w * (usize)h), *px = malloc(sizeof(f32) * (usize)w), *py = malloc(sizeof(f32) * (usize)h);
    if (!lum || !px || !py) abort();
    for (isize i = 0; i < (isize)w * h; ++i) lum[i] = luma(im->pixels + i * 4);
    static const i32 SIZES[] = { 19, 13, 9 };
    *out = (BoardGrid){ 0 };
    f32 best = 0.08f;   // below this, no grid (crowded 17 px boards score ~0.13, clean ones 0.3-0.8)
    for (i32 light = 0; light < 2; ++light) {   // dark lines on a light board, then light lines on a dark one
        line_profile(im, lum, true, light, px);
        line_profile(im, lum, false, light, py);
        for (i32 s = 0; s < 3; ++s) {
            if (size && SIZES[s] != size) continue;
            BoardGrid g = { .size = SIZES[s] };
            f32 sx = fit_axis(px, w, g.size, &g.x0, &g.step_x), sy = fit_axis(py, h, g.size, &g.y0, &g.step_y);
            g.score = fminf(sx, sy);
            // Square-ish cells (some clients stretch boards a little, never a lot).
            f32 ratio = g.step_x > g.step_y ? g.step_x / g.step_y : g.step_y / g.step_x;
            if (ratio > 1.15f) continue;
            if (g.score > best) {
                refine_axis(px, w, g.size, &g.x0, &g.step_x);
                refine_axis(py, h, g.size, &g.y0, &g.step_y);
                g.light_lines = light;
                g.strength = 0;
                for (i32 k = 0; k < g.size; ++k) g.strength += at(px, w, g.x0 + (f32)k * g.step_x, 1.0f) + at(py, h, g.y0 + (f32)k * g.step_y, 1.0f);
                g.strength /= (f32)(2 * g.size);
                best = g.score, *out = g;
            }
        }
    }
    free(lum), free(px), free(py);
    return out->size > 0;
}

f32 board_grid_strength(const RgbaImage *im, const BoardGrid *g) {
    i32 w = im->width, h = im->height;
    if (w < 2 * LINE_REACH + 1 || h < 2 * LINE_REACH + 1 || g->size <= 0) return 0;
    f32 *lum = malloc(sizeof(f32) * (usize)w * (usize)h), *px = malloc(sizeof(f32) * (usize)w), *py = malloc(sizeof(f32) * (usize)h);
    if (!lum || !px || !py) abort();
    for (isize i = 0; i < (isize)w * h; ++i) lum[i] = luma(im->pixels + i * 4);
    line_profile(im, lum, true, g->light_lines, px);
    line_profile(im, lum, false, g->light_lines, py);
    f32 sum = 0;
    for (i32 k = 0; k < g->size; ++k) sum += at(px, w, g->x0 + (f32)k * g->step_x, 1.0f) + at(py, h, g->y0 + (f32)k * g->step_y, 1.0f);
    free(lum), free(px), free(py);
    return sum / (f32)(2 * g->size);
}

// ---- Stones -----------------------------------------------------------------------------------------

typedef struct Rgb {
    f32 r, g, b;
} Rgb;

static Rgb sample(const RgbaImage *im, f32 fx, f32 fy) {   // 3x3 mean
    Rgb c = { 0 };
    i32 n = 0, cx = (i32)lroundf(fx), cy = (i32)lroundf(fy);
    for (i32 y = cy - 1; y <= cy + 1; ++y) {
        for (i32 x = cx - 1; x <= cx + 1; ++x) {
            if (x < 0 || y < 0 || x >= im->width || y >= im->height) continue;
            const u8 *p = im->pixels + ((usize)y * (usize)im->width + (usize)x) * 4;
            c.r += p[0], c.g += p[1], c.b += p[2], n++;
        }
    }
    if (n) c.r /= (f32)n, c.g /= (f32)n, c.b /= (f32)n;
    return c;
}

static f32 rgb_luma(Rgb c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
static f32 rgb_chroma(Rgb c) { return fmaxf(c.r, fmaxf(c.g, c.b)) - fminf(c.r, fminf(c.g, c.b)); }


void board_read_stones(const RgbaImage *im, const BoardGrid *g, BoardReading *out) {
    memset(out, 0, sizeof(*out));
    out->size = g->size;
    i32 n = g->size;
    // Board colour at every cell centre (a stone never reaches them). Each
    // intersection compares with the mean of its (up to) four neighbouring
    // centres: textured, gradient or unevenly lit boards stay "board".
    i32 cells_n = n - 1;
    f32 *cl = malloc(sizeof(f32) * (usize)(cells_n * cells_n)), *cc = malloc(sizeof(f32) * (usize)(cells_n * cells_n));
    if (!cl || !cc) abort();
    for (i32 j = 0; j < cells_n; ++j) {
        for (i32 i = 0; i < cells_n; ++i) {
            Rgb c = sample(im, g->x0 + ((f32)i + 0.5f) * g->step_x, g->y0 + ((f32)j + 0.5f) * g->step_y);
            cl[j * cells_n + i] = rgb_luma(c), cc[j * cells_n + i] = rgb_chroma(c);
        }
    }
    // Per intersection, the share of dark and bright pixels in a disc inside the stone
    // (after readboard's BoardOCR, but relative to this board's colour), skipping the grid
    // lines. Shares tolerate move numbers, last-move marks and highlights on stones.
    f32 radius_x = 0.42f * g->step_x, radius_y = 0.42f * g->step_y;
    for (i32 y = 0; y < n; ++y) {
        for (i32 x = 0; x < n; ++x) {
            f32 cx = g->x0 + (f32)x * g->step_x, cy = g->y0 + (f32)y * g->step_y;
            f32 board_l = 0, board_c = 0;
            i32 refs = 0;
            for (i32 j = y - 1; j <= y; ++j) {
                for (i32 i = x - 1; i <= x; ++i) {
                    if (i < 0 || j < 0 || i >= cells_n || j >= cells_n) continue;
                    board_l += cl[j * cells_n + i], board_c += cc[j * cells_n + i], refs++;
                }
            }
            board_l /= (f32)refs, board_c /= (f32)refs;
            f32 black_drop = fmaxf(0.35f * board_l, 12.0f);   // dark boards leave little room below them
            f32 white_rise = fmaxf(0.4f * (255.0f - board_l), 10.0f);   // light boards leave little room above them
            i32 dark = 0, bright = 0, total = 0;
            for (i32 py = (i32)floorf(cy - radius_y); py <= (i32)ceilf(cy + radius_y); ++py) {
                for (i32 qx = (i32)floorf(cx - radius_x); qx <= (i32)ceilf(cx + radius_x); ++qx) {
                    f32 ux = ((f32)qx - cx) / radius_x, uy = ((f32)py - cy) / radius_y;
                    if (ux * ux + uy * uy > 1.0f || fabsf((f32)qx - cx) < 2.0f || fabsf((f32)py - cy) < 2.0f) continue;
                    if (qx < 0 || py < 0 || qx >= im->width || py >= im->height) continue;
                    const u8 *p = im->pixels + ((usize)py * (usize)im->width + (usize)qx) * 4;
                    f32 l = luma(p), c = (f32)(MAX(p[0], MAX(p[1], p[2])) - MIN(p[0], MIN(p[1], p[2])));
                    total++;
                    if (board_l - l > black_drop) dark++;
                    else if (l - board_l > white_rise || (l > 150.0f && board_c - c > 18.0f && l - board_l > -5.0f)) bright++;
                }
            }
            f32 dark_share = total ? (f32)dark / (f32)total : 0, bright_share = total ? (f32)bright / (f32)total : 0;
            Stone s = dark_share >= 0.4f ? STONE_BLACK : bright_share >= 0.4f ? STONE_WHITE : STONE_EMPTY;
            out->cells[y * n + x] = (u8)s;
            f32 share = fmaxf(dark_share, bright_share);
            out->uncertain += share > 0.25f && share < 0.55f;   // neither clearly a stone nor clearly empty
        }
    }
    free(cl), free(cc);
}
