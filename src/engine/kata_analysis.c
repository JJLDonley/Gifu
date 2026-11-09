#include "engine/analysis.h"

#include <stdlib.h>
#include <string.h>

static f32 parse_f32(StringView token) {
    f64 v = 0;
    return sv_parse_f64(token, &v) ? (f32)v : 0.0f;
}

static i32 parse_i32(StringView token) {
    i64 v = 0;
    return sv_parse_i64(token, &v) ? (i32)v : 0;
}

static int compare_order(const void *a, const void *b) {
    const Candidate *x = a, *y = b;
    return (x->order > y->order) - (x->order < y->order);
}

static bool is_section_keyword(StringView t) {
    return sv_eq(t, SV("info")) || sv_eq(t, SV("ownership")) || sv_eq(t, SV("ownershipStdev")) ||
           sv_eq(t, SV("pvVisits")) || sv_eq(t, SV("pvEdgeVisits")) || sv_eq(t, SV("movesOwnership"));
}

bool kata_analysis_parse_line(StringView line, i32 width, i32 height, AnalysisSnapshot *out) {
    // Keep position_id / to_play set by the caller; reset the payload.
    out->width = width;
    out->height = height;
    out->total_visits = 0;
    out->candidate_count = 0;
    out->has_ownership = false;

    StringView rest = line;
    StringView token = sv_chop_token(&rest);
    Candidate *cand = NULL;

    while (token.len > 0) {
        if (sv_eq(token, SV("info"))) {
            cand = out->candidate_count < ANALYSIS_MAX_CANDIDATES ? &out->candidates[out->candidate_count++] : NULL;
            if (cand) memset(cand, 0, sizeof(*cand));
            token = sv_chop_token(&rest);
            continue;
        }
        if (sv_eq(token, SV("ownership"))) {
            i32 n = width * height;
            for (i32 i = 0; i < n; ++i) {
                StringView v = sv_chop_token(&rest);
                if (v.len == 0) break;
                out->ownership[i] = parse_f32(v);
            }
            out->has_ownership = true;
            cand = NULL;
            token = sv_chop_token(&rest);
            continue;
        }
        if (sv_eq(token, SV("pv")) && cand) {
            token = sv_chop_token(&rest);
            while (token.len > 0 && !is_section_keyword(token)) {
                Point p;
                if (cand->pv_len < ANALYSIS_MAX_PV && point_from_gtp(token, width, height, &p)) cand->pv[cand->pv_len++] = p;
                token = sv_chop_token(&rest);
            }
            continue;
        }
        if (sv_eq(token, SV("pvVisits")) || sv_eq(token, SV("pvEdgeVisits")) || sv_eq(token, SV("ownershipStdev")) ||
            sv_eq(token, SV("movesOwnership"))) {
            // Skip the numeric list that follows.
            token = sv_chop_token(&rest);
            while (token.len > 0 && !is_section_keyword(token) && !sv_eq(token, SV("pv"))) {
                char c = token.data[0];
                if (!(c == '-' || c == '.' || (c >= '0' && c <= '9'))) break;
                token = sv_chop_token(&rest);
            }
            continue;
        }

        StringView value = sv_chop_token(&rest);
        if (cand) {
            if (sv_eq(token, SV("move"))) {
                if (!point_from_gtp(value, width, height, &cand->move)) cand->move = POINT_PASS;
            } else if (sv_eq(token, SV("visits"))) {
                cand->visits = parse_i32(value);
            } else if (sv_eq(token, SV("order"))) {
                cand->order = parse_i32(value);
            } else if (sv_eq(token, SV("winrate"))) {
                cand->winrate = parse_f32(value);
            } else if (sv_eq(token, SV("scoreLead"))) {
                cand->score_lead = parse_f32(value);
            } else if (sv_eq(token, SV("scoreStdev"))) {
                cand->score_stdev = parse_f32(value);
            } else if (sv_eq(token, SV("prior"))) {
                cand->prior = parse_f32(value);
            } else if (sv_eq(token, SV("lcb"))) {
                cand->lcb = parse_f32(value);
            }
        }
        token = sv_chop_token(&rest);
    }

    if (out->candidate_count > 1) qsort(out->candidates, (usize)out->candidate_count, sizeof(Candidate), compare_order);
    for (i32 i = 0; i < out->candidate_count; ++i) out->total_visits += out->candidates[i].visits;
    return out->candidate_count > 0 || out->has_ownership;
}

bool kata_raw_nn_parse(StringView text, i32 width, i32 height, HumanPolicy *out) {
    out->width = width;
    out->height = height;
    out->pass = 0;
    out->white_win = 0.5f;
    bool have_policy = false;
    StringView rest = text;
    for (StringView token = sv_chop_token(&rest); token.len; token = sv_chop_token(&rest)) {
        if (sv_eq(token, SV("whiteWin"))) {
            out->white_win = parse_f32(sv_chop_token(&rest));
        } else if (sv_eq(token, SV("policyPass"))) {
            out->pass = parse_f32(sv_chop_token(&rest));
        } else if (sv_eq(token, SV("policy"))) {
            i32 n = width * height, i = 0;
            for (; i < n; ++i) {
                StringView v = sv_chop_token(&rest);
                if (v.len == 0) break;
                f32 p = parse_f32(v);   // "NAN" (occupied) parses as 0
                out->policy[i] = p == p && p > 0 ? p : 0.0f;
            }
            have_policy = i == n;
        }
    }
    return have_policy;
}
