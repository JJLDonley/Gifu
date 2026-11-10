#include "core/game_report.h"

#include <math.h>
#include <string.h>

const f32 SCORE_LOSS_BUCKETS[LOSS_BUCKETS - 1] = { 0.5f, 2.0f, 5.0f, 10.0f };
const f32 WINRATE_LOSS_BUCKETS[LOSS_BUCKETS - 1] = { 0.02f, 0.05f, 0.10f, 0.20f };

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

f32 move_accuracy(f32 score_loss) { return 100.0f * expf(-fmaxf(score_loss, 0.0f) / 8.0f); }

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

static void insert_worst(PlayerReport *p, GameNode *node, f32 loss) {
    i32 at = p->worst_count;
    while (at > 0 && p->worst_loss[at - 1] < loss) --at;
    if (at >= REPORT_WORST) return;
    i32 last = MIN(p->worst_count, REPORT_WORST - 1);
    for (i32 i = last; i > at; --i) {
        p->worst[i] = p->worst[i - 1];
        p->worst_loss[i] = p->worst_loss[i - 1];
    }
    p->worst[at] = node;
    p->worst_loss[at] = loss;
    if (p->worst_count < REPORT_WORST) p->worst_count++;
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
        f32 acc = move_accuracy(score_loss);
        p->moves++;
        st->moves++;
        wr_sum[c] += MAX(wr_loss, 0.0f);
        score_sum[c] += MAX(score_loss, 0.0f);
        acc_sum[c] += acc;
        stage_wr[phase][c] += MAX(wr_loss, 0.0f);
        stage_score[phase][c] += MAX(score_loss, 0.0f);
        stage_acc[phase][c] += acc;
        p->score_hist[bucket(SCORE_LOSS_BUCKETS, score_loss)]++;
        p->winrate_hist[bucket(WINRATE_LOSS_BUCKETS, wr_loss)]++;
        if (wr_loss > 0.10f) p->blunders++, st->blunders++;
        else if (wr_loss > 0.05f) p->mistakes++, st->mistakes++;
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
        if (wr_loss > 0.0f) insert_worst(p, n, wr_loss);
    }
    for (i32 c = STONE_BLACK; c <= STONE_WHITE; ++c) {
        PlayerReport *p = &out->player[c];
        if (p->moves) {
            p->avg_winrate_loss = (f32)(wr_sum[c] / p->moves);
            p->avg_score_loss = (f32)(score_sum[c] / p->moves);
            p->accuracy = (f32)(acc_sum[c] / p->moves);
        }
        if (p->agreement_moves) p->agreement = (f32)(agree_sum[c] / p->agreement_moves);
        for (i32 ph = 0; ph < GAME_PHASES; ++ph) {
            StageReport *st = &p->stage[ph];
            if (!st->moves) continue;
            st->avg_winrate_loss = (f32)(stage_wr[ph][c] / st->moves);
            st->avg_score_loss = (f32)(stage_score[ph][c] / st->moves);
            st->accuracy = (f32)(stage_acc[ph][c] / st->moves);
            if (st->agreement_moves) st->agreement = (f32)(stage_agree[ph][c] / st->agreement_moves);
        }
    }
}
