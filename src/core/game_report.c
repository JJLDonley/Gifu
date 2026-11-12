#include "core/game_report.h"

#include <math.h>
#include <string.h>

static LossLimits g_limits = { { 0.5f, 2.0f, 5.0f, 10.0f }, { 0.02f, 0.05f, 0.10f, 0.20f } };

LossLimits loss_limits_default(void) { return (LossLimits){ { 0.5f, 2.0f, 5.0f, 10.0f }, { 0.02f, 0.05f, 0.10f, 0.20f } }; }
const LossLimits *loss_limits(void) { return &g_limits; }

static bool increasing(const f32 *v) {
    for (i32 i = 0; i < LOSS_BUCKETS - 1; ++i) {
        if (!(v[i] > 0.0f) || (i > 0 && v[i] <= v[i - 1])) return false;
    }
    return true;
}

void loss_limits_set(const LossLimits *limits) {
    if (increasing(limits->score)) memcpy(g_limits.score, limits->score, sizeof(g_limits.score));
    if (increasing(limits->winrate)) memcpy(g_limits.winrate, limits->winrate, sizeof(g_limits.winrate));
}

static void parse_list(StringView text, f32 scale, f32 *out) {
    f32 v[LOSS_BUCKETS - 1];
    i32 n = 0;
    while (text.len && n < LOSS_BUCKETS - 1) {
        f64 x = 0;
        if (!sv_parse_f64(sv_trim(sv_chop_char(&text, ',')), &x)) return;
        v[n++] = (f32)(x * scale);
    }
    if (n == LOSS_BUCKETS - 1 && increasing(v)) memcpy(out, v, sizeof(v));
}

void loss_limits_parse(StringView winrate_percent, StringView score, LossLimits *out) {
    *out = loss_limits_default();
    parse_list(winrate_percent, 0.01f, out->winrate);
    parse_list(score, 1.0f, out->score);
}

String loss_limits_format(const f32 *limits, f32 scale) {
    String s = { 0 };
    for (i32 i = 0; i < LOSS_BUCKETS - 1; ++i) string_pushf(&s, "%s%g", i ? "," : "", (f64)(limits[i] * scale));
    return s;
}

bool loss_is_mistake(f32 winrate_loss) { return winrate_loss >= g_limits.winrate[1] && winrate_loss < g_limits.winrate[2]; }
bool loss_is_blunder(f32 winrate_loss) { return winrate_loss >= g_limits.winrate[2]; }

i32 game_phase_start(i32 phase, i32 board_points) {
    f64 scale = (f64)(board_points > 0 ? board_points : 361) / 361.0;
    if (phase <= GAME_PHASE_OPENING) return 1;
    if (phase == GAME_PHASE_MIDDLE) return 1 + (i32)lround(50 * scale);
    return 1 + (i32)lround(150 * scale);
}

i32 game_phase(i32 move_number, i32 board_points) {
    if (move_number >= game_phase_start(GAME_PHASE_ENDGAME, board_points)) return GAME_PHASE_ENDGAME;
    if (move_number >= game_phase_start(GAME_PHASE_MIDDLE, board_points)) return GAME_PHASE_MIDDLE;
    return GAME_PHASE_OPENING;
}

bool node_match_value(const GameNode *node, f32 *ratio, i32 *rank) {
    if (!node->has_move || !node->parent || !node->parent->eval.valid) return false;
    const NodeEval *e = &node->parent->eval;
    if (e->top_count == 0 || e->top_share[0] <= 0) return false;
    f32 best = 0;
    for (i32 k = 0; k < e->top_count; ++k) best = fmaxf(best, e->top_share[k]);
    *ratio = 0, *rank = 0;
    for (i32 k = 0; k < e->top_count; ++k) {
        if (!point_eq(e->top[k], node->move.point)) continue;
        *rank = k + 1;
        *ratio = k == 0 ? 1.0f : e->top_share[k] / best;   // KataGo's first choice counts 1 (as in LizzieYzy)
        break;
    }
    return true;
}

bool node_accuracy(const GameNode *node, f32 *accuracy) {
    f32 ratio = 0;
    i32 rank = 0;
    if (!node_match_value(node, &ratio, &rank)) return false;
    *accuracy = ratio * 100.0f;
    return true;
}

static MatchRule g_match = { 3, 20.0f };
const MatchRule *match_rule(void) { return &g_match; }
void match_rule_set(MatchRule rule) {
    g_match.moves = CLAMP(rule.moves, 1, NODE_EVAL_TOP);
    g_match.percent = CLAMP(rule.percent, 0.0f, 100.0f);
}

bool node_matches_ai(const GameNode *node) {
    f32 ratio = 0;
    i32 rank = 0;
    return node_match_value(node, &ratio, &rank) && rank > 0 && rank <= g_match.moves && ratio * 100.0f >= g_match.percent - 1e-4f;
}

bool node_played_share(const GameNode *node, f32 *share) {
    if (!node->has_move || !node->parent || !node->parent->eval.valid) return false;
    const NodeEval *e = &node->parent->eval;
    if (e->top_count == 0 || e->top_share[0] <= 0) return false;
    *share = 0;
    for (i32 k = 0; k < e->top_count; ++k) {
        if (point_eq(e->top[k], node->move.point)) *share = e->top_share[k];
    }
    return true;
}

static i32 bucket(const f32 *bounds, f32 value) {
    i32 b = 0;
    while (b < LOSS_BUCKETS - 1 && value >= bounds[b]) ++b;
    return b;
}

// Keeps the REPORT_WORST biggest losses, worst first.
static void insert_worst(GameNode **nodes, f32 *losses, i32 *count, GameNode *node, f32 loss) {
    i32 at = *count;
    while (at > 0 && losses[at - 1] < loss) --at;
    if (at >= REPORT_WORST) return;
    i32 last = MIN(*count, REPORT_WORST - 1);
    for (i32 i = last; i > at; --i) {
        nodes[i] = nodes[i - 1];
        losses[i] = losses[i - 1];
    }
    nodes[at] = node;
    losses[at] = loss;
    if (*count < REPORT_WORST) ++*count;
}

void game_report_compute(GameNode *const *line, isize count, GameReport *out) {
    memset(out, 0, sizeof(*out));
    f64 wr_sum[3] = { 0 }, score_sum[3] = { 0 }, acc_sum[3] = { 0 };
    f64 agree_sum[3] = { 0 }, stage_agree[GAME_PHASES][3] = { 0 };
    f64 stage_wr[GAME_PHASES][3] = { 0 }, stage_score[GAME_PHASES][3] = { 0 }, stage_acc[GAME_PHASES][3] = { 0 };
    for (isize i = 0; i < count; ++i) {
        GameNode *n = line[i];
        if (n->eval.valid) out->analysed_nodes++;
        if (!n->has_move) continue;
        out->total_moves++;
        f32 wr_loss = 0, score_loss = 0;
        if (!game_node_winrate_loss(n, &wr_loss)) continue;
        game_node_score_loss(n, &score_loss);
        Stone c = n->move.color;
        PlayerReport *p = &out->player[c];
        i32 phase = game_phase(n->move_number, n->board.width * n->board.height);
        StageReport *st = &p->stage[phase];
        p->moves++;
        st->moves++;
        wr_sum[c] += MAX(wr_loss, 0.0f);
        score_sum[c] += MAX(score_loss, 0.0f);
        f32 acc = 0;
        if (node_accuracy(n, &acc)) {   // Hawk Eye: over the moves whose candidates' visits are known
            acc_sum[c] += acc, stage_acc[phase][c] += acc;
            p->match_moves++, st->match_moves++;
            if (node_matches_ai(n)) p->matches++, st->matches++;
        }
        stage_wr[phase][c] += MAX(wr_loss, 0.0f);
        stage_score[phase][c] += MAX(score_loss, 0.0f);
        p->score_hist[bucket(g_limits.score, score_loss)]++;
        p->winrate_hist[bucket(g_limits.winrate, wr_loss)]++;
        if (loss_is_blunder(wr_loss)) p->blunders++, st->blunders++;
        else if (loss_is_mistake(wr_loss)) p->mistakes++, st->mistakes++;
        f32 share = 0;
        if (node_played_share(n, &share)) {
            agree_sum[c] += share * 100.0f, p->agreement_moves++;
            stage_agree[phase][c] += share * 100.0f, st->agreement_moves++;
        }
        const NodeEval *before = &n->parent->eval;
        for (i32 k = 0; k < before->top_count && k < 3; ++k) {
            if (!point_eq(before->top[k], n->move.point)) continue;
            if (k == 0) p->top1++, st->top1++;
            p->top3++;
            st->top3++;
            break;
        }
        if (wr_loss > 0.0f) insert_worst(p->worst, p->worst_loss, &p->worst_count, n, wr_loss);
        f32 points = 0;
        if (game_node_score_loss(n, &points) && points > 0.0f) insert_worst(p->worst_points, p->worst_points_loss, &p->worst_points_count, n, points);
    }
    for (i32 c = STONE_BLACK; c <= STONE_WHITE; ++c) {
        PlayerReport *p = &out->player[c];
        if (p->moves) {
            p->avg_winrate_loss = (f32)(wr_sum[c] / p->moves);
            p->avg_score_loss = (f32)(score_sum[c] / p->moves);
        }
        if (p->match_moves) p->accuracy = (f32)(acc_sum[c] / p->match_moves);
        if (p->agreement_moves) p->agreement = (f32)(agree_sum[c] / p->agreement_moves);
        for (i32 ph = 0; ph < GAME_PHASES; ++ph) {
            StageReport *st = &p->stage[ph];
            if (!st->moves) continue;
            st->avg_winrate_loss = (f32)(stage_wr[ph][c] / st->moves);
            st->avg_score_loss = (f32)(stage_score[ph][c] / st->moves);
            if (st->match_moves) st->accuracy = (f32)(stage_acc[ph][c] / st->match_moves);
            if (st->agreement_moves) st->agreement = (f32)(stage_agree[ph][c] / st->agreement_moves);
        }
    }
}
