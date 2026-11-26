// console.h - the engine console's lines (View -> Console): what Gifu sends its engines, their replies and log
// output (KataGo's start-up: backend, OpenCL tuning, network loading), and Gifu's own log. A fixed ring of the last
// CONSOLE_CAPACITY lines, safe to add to from any thread; the UI reads it by sequence number.
#pragma once

#include "base/str.h"

enum { CONSOLE_CAPACITY = 4000 };

typedef enum ConsoleKind {
    CONSOLE_SENT,        // a command sent to an engine
    CONSOLE_REPLY,       // an engine's reply
    CONSOLE_ENGINE_LOG,  // an engine's stderr (start-up, warnings)
    CONSOLE_ANALYSIS,    // streaming analysis lines (many per second; kept only while console_capture_analysis)
    CONSOLE_APP,         // Gifu's own log
    CONSOLE_KIND_COUNT,
} ConsoleKind;

typedef struct ConsoleLine {
    i64 seq;             // 1, 2, 3 ... across the whole run
    f64 time;            // seconds since the first line
    ConsoleKind kind;
    char source[24];     // "katago", "leela", "gifu"
    String text;         // owned by the copy returned from console_get
} ConsoleLine;

void console_add(ConsoleKind kind, StringView source, StringView text);
// The newest line's sequence number (0 = none yet), and the oldest still kept.
i64 console_last_seq(void);
i64 console_first_seq(void);
// A copy of line `seq` (free its text with string_free); false when it is no longer kept.
bool console_get(i64 seq, ConsoleLine *out);
void console_clear(void);
// Streaming analysis lines are dropped unless this is on (the UI turns it on while the console shows them).
void console_set_capture_analysis(bool on);
bool console_capture_analysis(void);
