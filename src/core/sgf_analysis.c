#include "core/sgf_analysis.h"

#include "base/codec.h"
#include "base/json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static StringView next_token(StringView *rest) {
    StringView s = sv_trim_left(*rest);
    isize n = 0;
    while (n < s.len && s.data[n] != ' ' && s.data[n] != '\t' && s.data[n] != '\n' && s.data[n] != '\r') ++n;
    *rest = sv_drop(s, n);
    return sv_take(s, n);
}

// "500", "1.2k", "3m" -> visits.
static i32 parse_visits(StringView s) {
    f64 scale = 1;
    if (s.len && (s.data[s.len - 1] == 'k' || s.data[s.len - 1] == 'K')) scale = 1e3, s.len--;
    else if (s.len && (s.data[s.len - 1] == 'm' || s.data[s.len - 1] == 'M')) scale = 1e6, s.len--;
    f64 v = 0;
    return sv_parse_f64(s, &v) ? (i32)llround(v * scale) : 0;
}

bool lz_parse(StringView text, Stone to_play, i32 w, i32 h, NodeEval *out) {
    *out = (NodeEval){ 0 };
    isize nl = sv_find_char(text, '\n');
    StringView line1 = nl >= 0 ? sv_take(text, nl) : text, line2 = nl >= 0 ? sv_drop(text, nl + 1) : SV("");
    StringView rest = line1;
    next_token(&rest);   // engine name
    f64 mover = 0, score = 0;
    if (!sv_parse_f64(next_token(&rest), &mover)) return false;
    out->visits = parse_visits(next_token(&rest));
    StringView score_tok = next_token(&rest);
    bool has_score = score_tok.len && sv_parse_f64(score_tok, &score);
    f64 stm = 1.0 - mover / 100.0;   // side to move at this node
    out->black_winrate = (f32)(to_play == STONE_WHITE ? 1.0 - stm : stm);
    out->black_score = has_score ? (f32)(to_play == STONE_WHITE ? -score : score) : 0.0f;
    out->no_score = !has_score;   // Leela Zero data has no score
    // Best moves, in order: every "move X" token.
    rest = line2;
    bool have_move = false;
    for (StringView tok = next_token(&rest); tok.len; tok = next_token(&rest)) {
        if (sv_eq(tok, SV("move"))) {
            Point p;
            have_move = out->top_count < NODE_EVAL_TOP && point_from_gtp(next_token(&rest), w, h, &p);
            if (have_move) out->top[out->top_count++] = p;
        } else if (sv_eq(tok, SV("visits")) && have_move && out->visits > 0) {   // share of the search
            out->top_share[out->top_count - 1] = (f32)CLAMP((f64)parse_visits(next_token(&rest)) / out->visits, 0.0, 1.0);
        }
    }
    out->valid = out->visits > 0;
    return out->valid;
}

String lz_format(const NodeEval *e, Stone to_play, i32 w, i32 h) {
    f64 stm = to_play == STONE_WHITE ? 1.0 - e->black_winrate : e->black_winrate;
    f64 stm_score = to_play == STONE_WHITE ? -e->black_score : e->black_score;
    // Without a score (Leela) the score fields are left out, as Leela Zero writes them.
    String out = e->no_score ? string_format("Leela %.1f %d\n", (1.0 - stm) * 100.0, e->visits)
                             : string_format("KataGo %.1f %d %.1f 0.0\n", (1.0 - stm) * 100.0, e->visits, stm_score);
    if (e->top_count > 0) {
        char coord[8];
        point_to_gtp(e->top[0], w, h, coord);
        if (e->no_score) string_pushf(&out, "move %s visits %d winrate %d prior 0 pv %s", coord, e->visits, (i32)(stm * 10000.0), coord);
        else string_pushf(&out, "move %s visits %d winrate %d prior 0 scoreMean %.2f pv %s", coord, e->visits, (i32)(stm * 10000.0), stm_score, coord);
    }
    return out;
}

bool kt_parse(const StringView *values, i32 count, i32 w, i32 h, NodeEval *out) {
    *out = (NodeEval){ 0 };
    if (count < 3) return false;
    String b64 = { 0 }, raw = { 0 };
    bool ok = base64_decode(values[2], &b64) && gzip_decompress(string_sv(&b64), &raw);
    JsonDoc doc = { 0 };
    if (ok) ok = json_parse(string_sv(&raw), &doc, NULL);
    if (ok) {
        JsonValue root = json_get(json_root(&doc), "root");
        ok = json_is_object(root);
        if (ok) {
            out->black_winrate = (f32)json_number(json_get(root, "winrate"), 0.5);
            out->black_score = (f32)json_number(json_get(root, "scoreLead"), 0.0);
            out->visits = (i32)json_int(json_get(root, "visits"), 0);
            // Top moves by "order".
            JsonValue moves = json_get(json_root(&doc), "moves");
            for (i32 rank = 0; rank < NODE_EVAL_TOP; ++rank) {
                JsonObjectIter it;
                json_object_iter_init(moves, &it);
                StringView key;
                JsonValue m;
                while (json_object_iter_next(&it, &key, &m)) {
                    if (json_int(json_get(m, "order"), -1) != rank) continue;
                    Point p;
                    if (point_from_gtp(json_string(json_get(m, "move"), key), w, h, &p)) {
                        f64 visits = json_number(json_get(m, "visits"), 0);
                        out->top_share[out->top_count] = out->visits > 0 ? (f32)CLAMP(visits / out->visits, 0.0, 1.0) : 0.0f;
                        out->top[out->top_count++] = p;
                    }
                    break;
                }
            }
            out->valid = true;
        }
        json_free(&doc);
    }
    string_free(&b64);
    string_free(&raw);
    return ok && out->valid;
}

static void pack(StringView raw, String *out) {
    String gz = { 0 };
    gzip_compress_stored(raw, &gz);
    base64_encode(string_sv(&gz), out);
    string_free(&gz);
}

void kt_format(const NodeEval *e, i32 w, i32 h, String out[3]) {
    for (i32 i = 0; i < 3; ++i) out[i] = (String){ 0 };
    pack(SV(""), &out[0]);   // ownership: not kept
    pack(SV(""), &out[1]);   // policy: not kept
    String json = string_from(SV("{\"moves\": {"));
    if (e->top_count > 0) {
        char coord[8];
        point_to_gtp(e->top[0], w, h, coord);
        string_pushf(&json, "\"%s\": {\"move\": \"%s\", \"order\": 0, \"visits\": %d, \"winrate\": %.6f, \"scoreLead\": %.4f, \"pv\": [\"%s\"]}",
                     coord, coord, e->visits, (f64)e->black_winrate, (f64)e->black_score, coord);
    }
    string_pushf(&json, "}, \"root\": {\"winrate\": %.6f, \"scoreLead\": %.4f, \"visits\": %d}, \"completed\": true}",
                 (f64)e->black_winrate, (f64)e->black_score, e->visits);
    pack(string_sv(&json), &out[2]);
    string_free(&json);
}

void game_tree_store_interop(GameTree *tree, bool lz, bool kt) {
    if (!lz && !kt) return;
    i32 w = game_tree_width(tree), h = game_tree_height(tree);
    if (kt) game_node_set_prop(tree, tree->root, SV("KTV"), SV("1.0"));
    GameNode *node = tree->root;
    while (node) {
        // KataGo's evaluation for both (KaTrain expects scores); Leela's for LZ[] when
        // KataGo did not analyse the position.
        const NodeEval *kata = &node->evals[EVAL_KATAGO], *any = kata->valid ? kata : &node->evals[EVAL_LEELA];
        if (any->valid) {
            if (lz) {
                String text = lz_format(any, node->to_play, w, h);
                game_node_set_prop(tree, node, SV("LZ"), string_sv(&text));
                string_free(&text);
            }
            if (kt && kata->valid) {
                String parts[3];
                kt_format(kata, w, h, parts);
                game_node_remove_prop(tree, node, SV("KT"));
                for (i32 i = 0; i < 3; ++i) {
                    game_node_add_prop_value(tree, node, SV("KT"), string_sv(&parts[i]));
                    string_free(&parts[i]);
                }
            }
        }
        if (node->first_child) {
            node = node->first_child;
            continue;
        }
        while (node && !node->next_sibling) node = node->parent;
        if (node) node = node->next_sibling;
    }
}
