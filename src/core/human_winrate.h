// human_winrate.h - the chance that a human of a given strength wins from a score
// lead, after Rendu, "Go Players Should Not Trust AI Win Rate", Journal of Go
// Studies 17(2), 2023 (fitted on Fox games 12k-9d, moves 50-200, mean error 1.2%):
//
//   f(x) = 1/2 * g x / (1 + |g x|^k)^(1/k) + 1/2        x: the score lead
//   k(m) = 1.99 - 0.00557 m                             m: the move number
//   g(m, L) = 0.0001 e^(w(L) m) + d(L)
//   w(L) = 0.0375 + 0.000543 L,  d(L) = 0.00292 e^(0.354 L) + 0.025
//
// L is the player level on Fox ranks: -11 = 12 kyu ... 0 = 1 kyu, 1 = 1 dan ... 9 = 9 dan.
#pragma once

#include "base/base.h"

enum { HUMAN_WR_FIRST_MOVE = 50, HUMAN_WR_LAST_MOVE = 200 };   // where the fit was checked

// Black's win chance (0..1) for players of level `level`, from Black's score lead.
f32 human_black_winrate(f32 black_score_lead, i32 move_number, f32 level);
// Fox rank -> level: 12 kyu = -11, 1 kyu = 0, 1 dan = 1, 9 dan = 9.
f32 human_level_for_kyu(i32 kyu);
f32 human_level_for_dan(i32 dan);
