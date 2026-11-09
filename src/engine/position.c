#include "engine/engine.h"

#include <string.h>

const char *engine_status_name(EngineStatus status) {
    switch (status) {
        case ENGINE_STATUS_STOPPED: return "stopped";
        case ENGINE_STATUS_STARTING: return "starting";
        case ENGINE_STATUS_READY: return "ready";
        case ENGINE_STATUS_FAILED: return "failed";
    }
    return "?";
}

static bool has_setup_props(const GameNode *node) {
    return game_node_find_prop(node, SV("AB")) || game_node_find_prop(node, SV("AW")) ||
           game_node_find_prop(node, SV("AE"));
}

void position_spec_from_node(const GameTree *tree, const GameNode *node, StringView rules, PositionSpec *out) {
    out->width = game_tree_width(tree);
    out->height = game_tree_height(tree);
    out->komi = (f32)game_tree_komi(tree);
    out->rules = rules;
    out->to_play = node->to_play;
    out->setup_count = 0;
    out->move_count = 0;

    // Collect the root..node path (deepest first), then walk it forward.
    const GameNode *path[1024 + 1];
    i32 depth = 0;
    for (const GameNode *n = node; n && depth < (i32)countof(path); n = n->parent) path[depth++] = n;

    // The last node on the path carrying setup stones becomes the base position.
    i32 base = depth - 1;   // root
    for (i32 i = 0; i < depth; ++i) {
        if (has_setup_props(path[i])) {
            base = i;
            break;
        }
    }
    const GameNode *base_node = path[base];
    if (has_setup_props(base_node)) {
        const Board *b = &base_node->board;
        for (i32 i = 0; i < b->width * b->height; ++i) {
            if (b->cells[i] == STONE_EMPTY) continue;
            out->setup[out->setup_count++] = (Move){ (Stone)b->cells[i], board_point(b, i) };
        }
    } else if (base_node->has_move) {
        out->moves[out->move_count++] = base_node->move;
    }
    for (i32 i = base - 1; i >= 0 && out->move_count < (i32)countof(out->moves); --i) {
        if (path[i]->has_move) out->moves[out->move_count++] = path[i]->move;
    }

    // Stable identity for this position (setup + moves + size + komi + side to move).
    u64 h = 1469598103934665603ull;
    h = (h ^ (u64)out->width) * 1099511628211ull;
    h = (h ^ (u64)out->height) * 1099511628211ull;
    h = (h ^ (u64)(i64)(out->komi * 2)) * 1099511628211ull;
    h = (h ^ (u64)out->to_play) * 1099511628211ull;
    for (i32 i = 0; i < out->setup_count; ++i) {
        h = (h ^ (u64)(out->setup[i].color * 1000 + out->setup[i].point.x * 31 + out->setup[i].point.y)) * 1099511628211ull;
    }
    h = (h ^ 0xABCDu) * 1099511628211ull;
    for (i32 i = 0; i < out->move_count; ++i) {
        h = (h ^ (u64)(out->moves[i].color * 1000 + (out->moves[i].point.x + 1) * 31 + out->moves[i].point.y + 1)) *
            1099511628211ull;
    }
    out->id = h;
}
