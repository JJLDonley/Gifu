// GTP framing over a LineChannel: "=id text" / "?id text" responses ending
// with a blank line; "info ..." lines inside a response (kata-analyze) are
// surfaced immediately as GTP_EVENT_INFO.
#include "engine/gtp_client.h"

#include "base/log.h"
#include "engine/line_channel.h"

#include <stdlib.h>

struct GtpClient {
    LineChannel *channel;
    i32 next_id;
    // Response being assembled.
    bool in_response;
    i32 id;
    bool success;
    String body;
};

GtpClient *gtp_client_start(const ProcessSpec *spec, String *error) {
    LineChannel *channel = line_channel_start(spec, error);
    if (!channel) return NULL;
    GtpClient *client = calloc(1, sizeof(GtpClient));
    if (!client) abort();
    client->channel = channel;
    client->next_id = 1;
    return client;
}

i32 gtp_client_send(GtpClient *client, StringView command) {
    if (!line_channel_alive(client->channel)) return -1;
    i32 id = client->next_id++;
    String line = string_format("%d " SV_FMT, id, SV_ARG(command));
    log_debug("gtp> %s", string_cstr(&line));
    bool ok = line_channel_send(client->channel, string_sv(&line));
    string_free(&line);
    return ok ? id : -1;
}

// Feeds one stdout line; returns true and fills `out` when it completes an event.
static bool frame_line(GtpClient *c, StringView line, GtpEvent *out) {
    if (!c->in_response) {
        if (line.len == 0) return false;
        char marker = line.data[0];
        if (marker != '=' && marker != '?') {
            log_debug("gtp: stray output: " SV_FMT, SV_ARG(line));
            return false;
        }
        StringView rest = sv_drop(line, 1);
        isize digits = 0;
        while (digits < rest.len && rest.data[digits] >= '0' && rest.data[digits] <= '9') ++digits;
        i64 id = -1;
        if (digits > 0 && !sv_parse_i64(sv_take(rest, digits), &id)) id = -1;
        c->in_response = true;
        c->id = (i32)id;
        c->success = marker == '=';
        string_clear(&c->body);
        string_push(&c->body, sv_trim(sv_drop(rest, digits)));
        return false;
    }
    if (line.len == 0) {
        *out = (GtpEvent){ GTP_EVENT_RESPONSE, c->id, c->success, string_from(string_sv(&c->body)) };
        c->in_response = false;
        return true;
    }
    if (sv_starts_with(line, SV("info ")) || sv_starts_with(line, SV("ownership "))) {
        *out = (GtpEvent){ GTP_EVENT_INFO, c->id, true, string_from(line) };
        return true;
    }
    if (c->body.len > 0) string_push_char(&c->body, '\n');
    string_push(&c->body, line);
    return false;
}

bool gtp_client_poll(GtpClient *client, GtpEvent *out) {
    Line line;
    while (line_channel_poll(client->channel, &line)) {
        bool ready = false;
        switch (line.kind) {
            case LINE_STDOUT: ready = frame_line(client, string_sv(&line.text), out); break;
            case LINE_STDERR:
                *out = (GtpEvent){ GTP_EVENT_STDERR, 0, true, string_from(string_sv(&line.text)) };
                ready = true;
                break;
            case LINE_EXITED:
                *out = (GtpEvent){ GTP_EVENT_EXITED, 0, false, { 0 } };
                ready = true;
                break;
        }
        line_free(&line);
        if (ready) return true;
    }
    return false;
}

void gtp_event_free(GtpEvent *event) { string_free(&event->text); }

void gtp_client_stop(GtpClient *client) {
    if (!client) return;
    line_channel_stop(client->channel, SV("quit"));
    string_free(&client->body);
    free(client);
}
