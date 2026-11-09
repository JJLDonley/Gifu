// process.h - child process with piped stdin/stdout/stderr.
// Implementations: CreateProcessW (Windows), posix_spawn (Linux/macOS).
#pragma once

#include "base/str.h"

typedef struct Process Process;

typedef struct ProcessSpec {
    StringView program;
    const StringView *args;   // not including argv[0]
    isize arg_count;
    StringView working_dir;   // empty = inherit
} ProcessSpec;

Process *process_spawn(const ProcessSpec *spec, String *error);
// Blocking read. Returns bytes read, 0 on EOF, -1 on error. Thread-safe per stream.
isize process_read_stdout(Process *proc, char *buffer, isize capacity);
isize process_read_stderr(Process *proc, char *buffer, isize capacity);
bool process_write(Process *proc, StringView data);
void process_close_stdin(Process *proc);
void process_kill(Process *proc);
// Waits up to timeout_ms (-1 = forever). Returns true if exited.
bool process_wait(Process *proc, i32 timeout_ms, i32 *exit_code);
void process_destroy(Process *proc);   // kills if still running
