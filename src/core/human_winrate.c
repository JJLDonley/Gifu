#include "core/human_winrate.h"

#include <math.h>

f32 human_black_winrate(f32 x, i32 move_number, f32 level) {
    f64 m = (f64)move_number, L = (f64)level;
    f64 k = 1.99 - 0.00557 * m;
    if (k < 0.2) k = 0.2;   // past move ~320 the fit would turn k negative: keep a steep sigmoid
    f64 w = 0.0375 + 0.000543 * L;
    f64 d = 0.00292 * exp(0.354 * L) + 0.025;
    f64 g = 0.0001 * exp(w * m) + d;
    f64 gx = g * (f64)x;
    f64 wr = 0.5 * gx / pow(1.0 + pow(fabs(gx), k), 1.0 / k) + 0.5;
    return (f32)(wr < 0 ? 0 : wr > 1 ? 1 : wr);
}

f32 human_level_for_kyu(i32 kyu) { return (f32)(1 - kyu); }
f32 human_level_for_dan(i32 dan) { return (f32)dan; }
