#include "core/profile.h"

#include <ctype.h>

// a (weight wa) and b (weight wb) averaged.
static f32 mix(f32 a, i32 wa, f32 b, i32 wb) { return wa + wb > 0 ? (a * (f32)wa + b * (f32)wb) / (f32)(wa + wb) : 0.0f; }

static void add_stage(StageReport *to, const StageReport *from) {
    to->accuracy = mix(to->accuracy, to->moves, from->accuracy, from->moves);
    to->avg_winrate_loss = mix(to->avg_winrate_loss, to->moves, from->avg_winrate_loss, from->moves);
    to->avg_score_loss = mix(to->avg_score_loss, to->moves, from->avg_score_loss, from->moves);
    to->agreement = mix(to->agreement, to->agreement_moves, from->agreement, from->agreement_moves);
    to->moves += from->moves, to->top1 += from->top1, to->top3 += from->top3;
    to->mistakes += from->mistakes, to->blunders += from->blunders;
    to->agreement_moves += from->agreement_moves;
}

static void add_player(PlayerReport *to, const PlayerReport *from) {
    to->accuracy = mix(to->accuracy, to->moves, from->accuracy, from->moves);
    to->avg_winrate_loss = mix(to->avg_winrate_loss, to->moves, from->avg_winrate_loss, from->moves);
    to->avg_score_loss = mix(to->avg_score_loss, to->moves, from->avg_score_loss, from->moves);
    to->agreement = mix(to->agreement, to->agreement_moves, from->agreement, from->agreement_moves);
    to->moves += from->moves, to->top1 += from->top1, to->top3 += from->top3;
    to->mistakes += from->mistakes, to->blunders += from->blunders;
    to->agreement_moves += from->agreement_moves;
    for (i32 s = 0; s < GAME_PHASES; ++s) add_stage(&to->stage[s], &from->stage[s]);
    for (i32 b = 0; b < LOSS_BUCKETS; ++b) to->score_hist[b] += from->score_hist[b], to->winrate_hist[b] += from->winrate_hist[b];
    to->worst_count = 0;   // nodes of other games: not kept
}

void profile_add_game(ProfileTotals *t, const GameReport *game, Stone color, ProfileResult result) {
    if (color != STONE_BLACK && color != STONE_WHITE) return;
    const PlayerReport *p = &game->player[color];
    if (p->moves == 0) return;   // not analysed
    add_player(&t->report.player[color], p);
    t->report.analysed_nodes += game->analysed_nodes;
    t->report.total_moves += game->total_moves;
    t->games[color]++;
    if (result == PROFILE_RESULT_WIN) t->wins[color]++;
    if (result == PROFILE_RESULT_LOSS) t->losses[color]++;
}

void profile_combined(const ProfileTotals *t, PlayerReport *out) {
    *out = (PlayerReport){ 0 };
    add_player(out, &t->report.player[STONE_BLACK]);
    add_player(out, &t->report.player[STONE_WHITE]);
}

static bool eq_nocase(StringView a, StringView b) {
    if (a.len != b.len) return false;
    for (isize i = 0; i < a.len; ++i) {
        if (tolower((u8)a.data[i]) != tolower((u8)b.data[i])) return false;
    }
    return true;
}

bool profile_name_matches(StringView player, StringView names) {
    player = sv_trim(player);
    if (!player.len) return false;
    StringView rest = names;
    while (rest.len) {
        StringView name = sv_trim(sv_chop_char(&rest, ','));
        if (name.len && eq_nocase(name, player)) return true;
    }
    return false;
}

ProfileResult profile_result(StringView re, Stone color) {
    re = sv_trim(re);
    if (re.len < 2 || re.data[1] != '+') return PROFILE_RESULT_UNKNOWN;   // draws, "Void", "?"
    char w = (char)toupper((u8)re.data[0]);
    if (w != 'B' && w != 'W') return PROFILE_RESULT_UNKNOWN;
    bool black_won = w == 'B';
    return (black_won == (color == STONE_BLACK)) ? PROFILE_RESULT_WIN : PROFILE_RESULT_LOSS;
}

void profile_add_style(ProfileTotals *t, const StyleReport *game, Stone color) {
    if (color == STONE_BLACK || color == STONE_WHITE) style_add(&t->style[color], &game->player[color]);
}
