#include "core/game_report.h"

#include <string.h>

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
    f64 wr_sum[3] = { 0 }, score_sum[3] = { 0 };
    for (isize i = 0; i < count; ++i) {
        GameNode *n = line[i];
        if (n->eval.valid) out->analysed_nodes++;
        if (!n->has_move) continue;
        out->total_moves++;
        f32 wr_loss = 0, score_loss = 0;
        if (!game_node_winrate_loss(n, &wr_loss)) continue;
        game_node_score_loss(n, &score_loss);
        PlayerReport *p = &out->player[n->move.color];
        p->moves++;
        wr_sum[n->move.color] += MAX(wr_loss, 0.0f);
        score_sum[n->move.color] += MAX(score_loss, 0.0f);
        if (wr_loss > 0.10f) p->blunders++;
        else if (wr_loss > 0.05f) p->mistakes++;
        const NodeEval *before = &n->parent->eval;
        for (i32 k = 0; k < before->top_count; ++k) {
            if (!point_eq(before->top[k], n->move.point)) continue;
            if (k == 0) p->top1++;
            p->top3++;
            break;
        }
        if (wr_loss > 0.0f) insert_worst(p, n, wr_loss);
    }
    for (i32 c = STONE_BLACK; c <= STONE_WHITE; ++c) {
        PlayerReport *p = &out->player[c];
        if (p->moves) {
            p->avg_winrate_loss = (f32)(wr_sum[c] / p->moves);
            p->avg_score_loss = (f32)(score_sum[c] / p->moves);
        }
    }
}
