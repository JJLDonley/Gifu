// line_channel.h - a child process spoken to in lines.
//
// Owns the process and two reader threads that split stdout / stderr into
// lines and queue them. The owner sends lines and drains the queue from its
// own thread; it never blocks on the child. Protocols (GTP, KataGo's JSON
// analysis) are layered on top.
#pragma once

#include "platform/process.h"

typedef enum LineKind {
    LINE_STDOUT,
    LINE_STDERR,
    LINE_EXITED,   // stdout closed (process ended or crashed)
} LineKind;

typedef struct Line {
    LineKind kind;
    String text;   // owned; free with line_free (empty for LINE_EXITED)
} Line;

typedef struct LineChannel LineChannel;

LineChannel *line_channel_start(const ProcessSpec *spec, String *error);
bool line_channel_send(LineChannel *channel, StringView line);   // appends '\n'
bool line_channel_poll(LineChannel *channel, Line *out);
bool line_channel_alive(const LineChannel *channel);
void line_free(Line *line);
// Writes `farewell` (if any), closes stdin, waits briefly, then kills.
// Sends `farewell`, closes stdin and returns at once; the process is waited
// for (and killed if needed) on a background thread.
void line_channel_stop(LineChannel *channel, StringView farewell);
// Blocks until every stopped channel's process is gone (call before exiting).
void line_channel_wait_reapers(i32 timeout_ms);
