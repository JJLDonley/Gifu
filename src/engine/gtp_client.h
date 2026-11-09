// gtp_client.h - asynchronous GTP transport.
//
// Owns the engine process and two reader threads (stdout, stderr). The UI
// thread sends commands with gtp_client_send() and drains parsed events with
// gtp_client_poll() each frame; it never blocks on the engine.
#pragma once

#include "engine/process.h"

typedef enum GtpEventKind {
    GTP_EVENT_RESPONSE,   // complete "=id ..." or "?id ..." response
    GTP_EVENT_INFO,       // streaming analysis line ("info move ...") for command `id`
    GTP_EVENT_STDERR,     // one stderr line (engine log / progress)
    GTP_EVENT_EXITED,     // stdout closed
} GtpEventKind;

typedef struct GtpEvent {
    GtpEventKind kind;
    i32 id;
    bool success;
    String text;   // owned; free with gtp_event_free
} GtpEvent;

typedef struct GtpClient GtpClient;

GtpClient *gtp_client_start(const ProcessSpec *spec, String *error);
// Sends one command; returns its id (> 0) or -1 if the engine is gone.
i32 gtp_client_send(GtpClient *client, StringView command);
bool gtp_client_poll(GtpClient *client, GtpEvent *out);
void gtp_event_free(GtpEvent *event);
// Sends "quit", waits briefly, then kills. Frees the client.
void gtp_client_stop(GtpClient *client);
