// profile.h - a player's profile: their games' reports added up, separately for
// the games they played as Black and as White.
//
// The result is a GameReport whose Black / White columns are "you as Black" and
// "you as White" (so the Deep report's views show it as they show one game).
// Counts add up; averages are weighted by the moves they cover. The worst-move
// lists are per game and are not kept.
#pragma once

#include "core/game_report.h"
#include "core/style.h"

typedef enum ProfileResult { PROFILE_RESULT_UNKNOWN, PROFILE_RESULT_WIN, PROFILE_RESULT_LOSS } ProfileResult;

typedef struct ProfileTotals {
    GameReport report;    // player[STONE_BLACK]: you as Black; player[STONE_WHITE]: you as White
    i32 games[3];         // indexed by Stone (games with analysed moves)
    i32 wins[3], losses[3];
    StyleCounts style[3];  // the user's style as Black / White (profile_add_style)
} ProfileTotals;

// Adds one game in which the user played `color`.
void profile_add_game(ProfileTotals *t, const GameReport *game, Stone color, ProfileResult result);
// Adds the user's style counts from one game.
void profile_add_style(ProfileTotals *t, const StyleReport *game, Stone color);
// You as either colour (both columns merged).
void profile_combined(const ProfileTotals *t, PlayerReport *out);

// True when `player` (an SGF PB / PW) is one of `names` (comma-separated),
// ignoring case and surrounding spaces.
bool profile_name_matches(StringView player, StringView names);
// The user's result from an SGF RE ("B+3.5", "W+R", "0"...) when they played `color`.
ProfileResult profile_result(StringView re, Stone color);
