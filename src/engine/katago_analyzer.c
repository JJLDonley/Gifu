// KataGo JSON analysis engine: one JSON query per line in, one JSON result
// per analysed turn out. Winrates/scores are reported from Black's
// perspective (reportAnalysisWinratesAs=BLACK), matching NodeEval.
#include "engine/batch_analyzer.h"

#include "base/json.h"
#include "base/log.h"
#include "base/vec.h"
#include "platform/line_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void turn_result_free(TurnResult *r) {
    string_free(&r->job_id);
    string_free(&r->error);
    free(r->human_policy);
    r->human_policy = NULL;
}

// ---- Protocol -----------------------------------------------------------------------------

static void push_json_string(String *out, StringView s) {
    string_push_char(out, '"');
    for (isize i = 0; i < s.len; ++i) {
        char c = s.data[i];
        if (c == '"' || c == '\\') string_push_char(out, '\\');
        if ((u8)c < 0x20) {
            string_pushf(out, "\\u%04x", c);
            continue;
        }
        string_push_char(out, c);
    }
    string_push_char(out, '"');
}

static void push_move(String *out, Move m, i32 width, i32 height) {
    char coord[8];
    point_to_gtp(m.point, width, height, coord);
    string_pushf(out, "[\"%s\",\"%s\"]", m.color == STONE_WHITE ? "W" : "B", coord);
}

String katago_analysis_query(const AnalysisJob *job) {
    String q = string_from(SV("{\"id\":"));
    push_json_string(&q, job->id);
    string_push(&q, SV(",\"moves\":["));
    for (i32 i = 0; i < job->move_count; ++i) {
        if (i) string_push_char(&q, ',');
        push_move(&q, job->moves[i], job->width, job->height);
    }
    string_push(&q, SV("],\"initialStones\":["));
    for (i32 i = 0; i < job->setup_count; ++i) {
        if (i) string_push_char(&q, ',');
        push_move(&q, job->setup[i], job->width, job->height);
    }
    string_push(&q, SV("],\"rules\":"));
    push_json_string(&q, job->rules.len ? job->rules : SV("chinese"));
    string_pushf(&q, ",\"komi\":%g,\"boardXSize\":%d,\"boardYSize\":%d,\"maxVisits\":%d,\"initialPlayer\":\"%s\",\"analyzeTurns\":[",
                 job->komi, job->width, job->height, MAX(job->max_visits, 1), job->first_player == STONE_WHITE ? "W" : "B");
    if (job->turns) {
        for (i32 i = 0; i < job->turn_count; ++i) string_pushf(&q, i ? ",%d" : "%d", job->turns[i]);
    } else {
        for (i32 t = 0; t <= job->move_count; ++t) string_pushf(&q, t ? ",%d" : "%d", t);
    }
    string_push_char(&q, ']');
    if (job->include_human_policy) string_push(&q, SV(",\"includePolicy\":true"));
    if (job->human_profile.len) {
        string_push(&q, SV(",\"overrideSettings\":{\"humanSLProfile\":"));
        push_json_string(&q, job->human_profile);
        string_push_char(&q, '}');
    }
    string_push_char(&q, '}');
    return q;
}

static bool parse_root(JsonValue root, i32 width, i32 height, TurnResult *out) {
    *out = (TurnResult){ 0 };
    out->job_id = string_from(json_string(json_get(root, "id"), SV("")));
    JsonValue error = json_get(root, "error");
    if (json_exists(error)) {
        out->turn = -1;
        out->error = string_from(json_string(error, SV("error")));
        return true;
    }
    if (!json_exists(json_get(root, "turnNumber"))) {   // warnings, "noResults", ...
        turn_result_free(out);
        return false;
    }
    out->turn = (i32)json_int(json_get(root, "turnNumber"), -1);
    JsonValue info = json_get(root, "rootInfo");
    out->eval = (NodeEval){
        .valid = true,
        .black_winrate = (f32)json_number(json_get(info, "winrate"), 0.5),
        .black_score = (f32)json_number(json_get(info, "scoreLead"), 0.0),
        .visits = (i32)json_int(json_get(info, "visits"), 0),
    };
    // Top moves by "order".
    JsonValue moves = json_get(root, "moveInfos");
    for (i32 rank = 0; rank < NODE_EVAL_TOP; ++rank) {
        for (isize i = 0; i < json_array_len(moves); ++i) {
            JsonValue m = json_array_at(moves, i);
            if (json_int(json_get(m, "order"), -1) != rank) continue;
            Point p;
            if (point_from_gtp(json_string(json_get(m, "move"), SV("")), width, height, &p)) {
                f64 visits = json_number(json_get(m, "visits"), 0);
                out->eval.top_share[out->eval.top_count] = out->eval.visits > 0 ? (f32)(visits / out->eval.visits) : 0.0f;
                out->eval.top[out->eval.top_count++] = p;
            }
            break;
        }
    }
    // Human SL policy (with includePolicy and a -human-model): row-major from the top-left, then pass; -1 = illegal.
    JsonValue human = json_get(root, "humanPolicy");
    isize points = (isize)width * height;
    if (json_array_len(human) == points + 1) {
        out->human_policy = malloc(sizeof(f32) * (usize)points);
        if (!out->human_policy) abort();
        for (isize i = 0; i < points; ++i) out->human_policy[i] = (f32)MAX(json_number(json_array_at(human, i), 0.0), 0.0);
        out->human_pass = (f32)MAX(json_number(json_array_at(human, points), 0.0), 0.0);
    }
    out->ok = true;
    return true;
}

bool katago_analysis_parse_response(StringView line, i32 width, i32 height, TurnResult *out) {
    JsonDoc doc = { 0 };
    if (!json_parse(line, &doc, NULL)) return false;
    bool ok = parse_root(json_root(&doc), width, height, out);
    json_free(&doc);
    return ok;
}

// ---- Implementation -------------------------------------------------------------------------

typedef struct JobInfo {
    String id;
    i32 width, height;
} JobInfo;

typedef struct KataGoAnalyzer {
    BatchAnalyzer base;
    LineChannel *channel;
    Vec(JobInfo) jobs;   // board size per job id (needed to parse coordinates)
} KataGoAnalyzer;

static bool ka_submit(BatchAnalyzer *a, const AnalysisJob *job) {
    KataGoAnalyzer *k = (KataGoAnalyzer *)a;
    vec_push(&k->jobs, (JobInfo){ string_from(job->id), job->width, job->height });
    String q = katago_analysis_query(job);
    bool ok = line_channel_send(k->channel, string_sv(&q));
    string_free(&q);
    return ok;
}

static bool ka_poll(BatchAnalyzer *a, TurnResult *out) {
    KataGoAnalyzer *k = (KataGoAnalyzer *)a;
    Line line;
    while (line_channel_poll(k->channel, &line)) {
        bool ready = false;
        if (line.kind == LINE_STDOUT && line.text.len > 0 && line.text.data[0] == '{') {
            JsonDoc doc = { 0 };
            if (json_parse(string_sv(&line.text), &doc, NULL)) {
                // Coordinates need the job's board size: look it up by id.
                StringView id = json_string(json_get(json_root(&doc), "id"), SV(""));
                i32 w = 19, h = 19;
                vec_foreach(JobInfo, j, &k->jobs) {
                    if (sv_eq(string_sv(&j->id), id)) w = j->width, h = j->height;
                }
                ready = parse_root(json_root(&doc), w, h, out);
            }
            json_free(&doc);
        } else if (line.kind == LINE_STDERR) {
            log_debug("analysis: " SV_FMT, SV_ARG(string_sv(&line.text)));
        } else if (line.kind == LINE_EXITED) {
            *out = (TurnResult){ .turn = -1, .ok = false, .error = string_from(SV("analysis engine exited")) };
            ready = true;
        }
        line_free(&line);
        if (ready) return true;
    }
    return false;
}

static void ka_cancel(BatchAnalyzer *a, StringView job_id) {
    KataGoAnalyzer *k = (KataGoAnalyzer *)a;
    String q = string_from(SV("{\"id\":\"cancel\",\"action\":\"terminate\",\"terminateId\":"));
    push_json_string(&q, job_id);
    string_push_char(&q, '}');
    line_channel_send(k->channel, string_sv(&q));
    string_free(&q);
}

static bool ka_alive(const BatchAnalyzer *a) { return line_channel_alive(((const KataGoAnalyzer *)a)->channel); }

static void ka_destroy(BatchAnalyzer *a) {
    KataGoAnalyzer *k = (KataGoAnalyzer *)a;
    line_channel_stop(k->channel, SV(""));
    vec_foreach(JobInfo, j, &k->jobs) string_free(&j->id);
    vec_free(&k->jobs);
    free(k);
}

static const BatchAnalyzerVTable KATAGO_ANALYZER_VTABLE = {
    .submit = ka_submit, .poll = ka_poll, .alive = ka_alive, .cancel = ka_cancel, .destroy = ka_destroy,
};

BatchAnalyzer *katago_analyzer_create(const KataGoAnalyzerSettings *s, String *error) {
    // Whole games are many positions at modest visits: favour parallel
    // positions (analysis threads) over deep search on each one, unless the user set them.
    String overrides = string_from(SV("reportAnalysisWinratesAs=BLACK"));
    if (sv_find(s->overrides, SV("numAnalysisThreads=")) < 0) string_push(&overrides, SV(",numAnalysisThreads=12"));
    if (sv_find(s->overrides, SV("numSearchThreadsPerAnalysisThread=")) < 0) string_push(&overrides, SV(",numSearchThreadsPerAnalysisThread=2"));
    string_push(&overrides, s->overrides);
    StringView args[] = {
        SV("analysis"), SV("-model"), s->model, SV("-config"), s->config, SV("-override-config"), string_sv(&overrides),
        SV("-human-model"), s->human_model,
    };
    isize argc = s->human_model.len ? countof(args) : countof(args) - 2;
    ProcessSpec spec = { s->program, args, argc, s->working_dir, s->launcher, s->launcher_count };
    LineChannel *channel = line_channel_start(&spec, error);
    string_free(&overrides);
    if (!channel) return NULL;
    KataGoAnalyzer *k = calloc(1, sizeof(KataGoAnalyzer));
    if (!k) abort();
    k->base.vtable = &KATAGO_ANALYZER_VTABLE;
    k->channel = channel;
    log_info("started analysis engine " SV_FMT, SV_ARG(s->program));
    return &k->base;
}
