#include "engine/gtp_client.h"

#include "base/log.h"
#include "base/vec.h"

#include <stdlib.h>
#include "base/thread.h"

typedef Vec(GtpEvent) GtpEventVec;

struct GtpClient {
    Process *process;
    Thread stdout_thread;
    Thread stderr_thread;
    Mutex lock;
    GtpEventVec queue;   // guarded by lock
    isize queue_head;    // guarded by lock
    i32 next_id;
    bool alive;
};

static void push_event(GtpClient *client, GtpEvent event) {
    mutex_lock(&client->lock);
    vec_push(&client->queue, event);
    mutex_unlock(&client->lock);
}

// Splits a byte stream into lines and feeds them to `on_line`.
typedef struct LineReader {
    String pending;
} LineReader;

typedef void (*LineHandler)(GtpClient *client, StringView line, void *state);

static void line_reader_feed(LineReader *reader, StringView chunk, GtpClient *client, LineHandler on_line, void *state) {
    string_push(&reader->pending, chunk);
    StringView rest = string_sv(&reader->pending);
    for (;;) {
        isize nl = sv_find_char(rest, '\n');
        if (nl < 0) break;
        StringView line = sv_take(rest, nl);
        if (line.len > 0 && line.data[line.len - 1] == '\r') line.len--;
        on_line(client, line, state);
        rest = sv_drop(rest, nl + 1);
    }
    String remaining = string_from(rest);
    string_free(&reader->pending);
    reader->pending = remaining;
}

// ---- stdout: GTP response framing ------------------------------------------------

typedef struct ResponseState {
    bool in_response;
    i32 id;
    bool success;
    String body;
} ResponseState;

static void on_stdout_line(GtpClient *client, StringView line, void *user) {
    ResponseState *st = user;
    if (!st->in_response) {
        if (line.len == 0) return;
        char marker = line.data[0];
        if (marker != '=' && marker != '?') {
            log_debug("gtp: stray output: " SV_FMT, SV_ARG(line));
            return;
        }
        StringView rest = sv_drop(line, 1);
        isize digits = 0;
        while (digits < rest.len && rest.data[digits] >= '0' && rest.data[digits] <= '9') ++digits;
        i64 id = -1;
        if (digits > 0 && !sv_parse_i64(sv_take(rest, digits), &id)) id = -1;
        st->in_response = true;
        st->id = (i32)id;
        st->success = marker == '=';
        string_clear(&st->body);
        string_push(&st->body, sv_trim(sv_drop(rest, digits)));
        return;
    }
    if (line.len == 0) {
        push_event(client, (GtpEvent){ GTP_EVENT_RESPONSE, st->id, st->success, string_from(string_sv(&st->body)) });
        st->in_response = false;
        return;
    }
    if (sv_starts_with(line, SV("info ")) || sv_starts_with(line, SV("ownership "))) {
        push_event(client, (GtpEvent){ GTP_EVENT_INFO, st->id, true, string_from(line) });
        return;
    }
    if (st->body.len > 0) string_push_char(&st->body, '\n');
    string_push(&st->body, line);
}

static i32 stdout_main(void *arg) {
    GtpClient *client = arg;
    LineReader reader = { 0 };
    ResponseState state = { 0 };
    char buffer[16384];
    for (;;) {
        isize n = process_read_stdout(client->process, buffer, (isize)sizeof(buffer));
        if (n <= 0) break;
        line_reader_feed(&reader, sv_from_parts(buffer, n), client, on_stdout_line, &state);
    }
    string_free(&reader.pending);
    string_free(&state.body);
    push_event(client, (GtpEvent){ GTP_EVENT_EXITED, 0, false, { 0 } });
    return 0;
}

// ---- stderr: plain lines -----------------------------------------------------------

static void on_stderr_line(GtpClient *client, StringView line, void *user) {
    UNUSED(user);
    if (line.len > 0) push_event(client, (GtpEvent){ GTP_EVENT_STDERR, 0, true, string_from(line) });
}

static i32 stderr_main(void *arg) {
    GtpClient *client = arg;
    LineReader reader = { 0 };
    char buffer[4096];
    for (;;) {
        isize n = process_read_stderr(client->process, buffer, (isize)sizeof(buffer));
        if (n <= 0) break;
        line_reader_feed(&reader, sv_from_parts(buffer, n), client, on_stderr_line, NULL);
    }
    string_free(&reader.pending);
    return 0;
}

// ---- Public API ----------------------------------------------------------------------

GtpClient *gtp_client_start(const ProcessSpec *spec, String *error) {
    Process *process = process_spawn(spec, error);
    if (!process) return NULL;
    GtpClient *client = calloc(1, sizeof(GtpClient));
    if (!client) abort();
    client->process = process;
    client->next_id = 1;
    client->alive = true;
    mutex_init(&client->lock);
    if (!thread_start(&client->stdout_thread, stdout_main, client) ||
        !thread_start(&client->stderr_thread, stderr_main, client)) {
        log_error("gtp: cannot start reader threads");
    }
    return client;
}

i32 gtp_client_send(GtpClient *client, StringView command) {
    if (!client->alive) return -1;
    i32 id = client->next_id++;
    String line = string_format("%d " SV_FMT "\n", id, SV_ARG(command));
    log_debug("gtp> %s", string_cstr(&line));
    bool ok = process_write(client->process, string_sv(&line));
    string_free(&line);
    if (!ok) {
        client->alive = false;
        return -1;
    }
    return id;
}

bool gtp_client_poll(GtpClient *client, GtpEvent *out) {
    bool got = false;
    mutex_lock(&client->lock);
    if (client->queue_head < client->queue.len) {
        *out = client->queue.items[client->queue_head++];
        got = true;
        if (out->kind == GTP_EVENT_EXITED) client->alive = false;
    }
    if (client->queue_head == client->queue.len) {
        client->queue_head = 0;
        vec_clear(&client->queue);
    }
    mutex_unlock(&client->lock);
    return got;
}

void gtp_event_free(GtpEvent *event) { string_free(&event->text); }

void gtp_client_stop(GtpClient *client) {
    if (!client) return;
    if (client->alive) process_write(client->process, SV("quit\n"));
    process_close_stdin(client->process);
    if (!process_wait(client->process, 1500, NULL)) {
        process_kill(client->process);
        process_wait(client->process, 2000, NULL);
    }
    thread_join(&client->stdout_thread);
    thread_join(&client->stderr_thread);
    process_destroy(client->process);
    for (isize i = client->queue_head; i < client->queue.len; ++i) gtp_event_free(&client->queue.items[i]);
    vec_free(&client->queue);
    mutex_destroy(&client->lock);
    free(client);
}
