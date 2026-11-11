#include "platform/line_channel.h"

#include "base/log.h"
#include "platform/os.h"
#include "platform/thread.h"
#include "base/vec.h"

#include <stdlib.h>

typedef Vec(Line) LineVec;

struct LineChannel {
    Process *process;
    Thread stdout_thread;
    Thread stderr_thread;
    Mutex lock;
    LineVec queue;     // guarded by lock
    isize queue_head;  // guarded by lock
    bool alive;
};

typedef struct Reader {
    LineChannel *channel;
    LineKind kind;
} Reader;

static void push(LineChannel *c, Line line) {
    mutex_lock(&c->lock);
    vec_push(&c->queue, line);
    mutex_unlock(&c->lock);
}

static i32 reader_main(LineChannel *c, LineKind kind) {
    String pending = { 0 };
    char buffer[16384];
    for (;;) {
        isize n = kind == LINE_STDOUT ? process_read_stdout(c->process, buffer, (isize)sizeof(buffer))
                                      : process_read_stderr(c->process, buffer, (isize)sizeof(buffer));
        if (n <= 0) break;
        string_push(&pending, sv_from_parts(buffer, n));
        StringView rest = string_sv(&pending);
        for (isize nl = sv_find_char(rest, '\n'); nl >= 0; nl = sv_find_char(rest, '\n')) {
            StringView line = sv_take(rest, nl);
            if (line.len > 0 && line.data[line.len - 1] == '\r') line.len--;
            // GTP needs blank stdout lines (they end responses); stderr blanks are noise.
            if (kind == LINE_STDOUT || line.len > 0) push(c, (Line){ kind, string_from(line) });
            rest = sv_drop(rest, nl + 1);
        }
        String remaining = string_from(rest);
        string_free(&pending);
        pending = remaining;
    }
    string_free(&pending);
    if (kind == LINE_STDOUT) push(c, (Line){ LINE_EXITED, { 0 } });
    return 0;
}

static i32 stdout_main(void *arg) { return reader_main(arg, LINE_STDOUT); }
static i32 stderr_main(void *arg) { return reader_main(arg, LINE_STDERR); }

LineChannel *line_channel_start(const ProcessSpec *spec, String *error) {
    Process *process = process_spawn(spec, error);
    if (!process) return NULL;
    LineChannel *c = calloc(1, sizeof(LineChannel));
    if (!c) abort();
    c->process = process;
    c->alive = true;
    mutex_init(&c->lock);
    if (!thread_start(&c->stdout_thread, stdout_main, c) || !thread_start(&c->stderr_thread, stderr_main, c)) {
        log_error("line channel: cannot start reader threads");
    }
    return c;
}

bool line_channel_send(LineChannel *c, StringView line) {
    if (!c->alive) return false;
    String buffer = string_from(line);
    string_push_char(&buffer, '\n');
    bool ok = process_write(c->process, string_sv(&buffer));
    string_free(&buffer);
    if (!ok) c->alive = false;
    return ok;
}

bool line_channel_poll(LineChannel *c, Line *out) {
    bool got = false;
    mutex_lock(&c->lock);
    if (c->queue_head < c->queue.len) {
        *out = c->queue.items[c->queue_head++];
        got = true;
        if (out->kind == LINE_EXITED) c->alive = false;
    }
    if (c->queue_head == c->queue.len) {
        c->queue_head = 0;
        vec_clear(&c->queue);
    }
    mutex_unlock(&c->lock);
    return got;
}

bool line_channel_alive(const LineChannel *c) { return c->alive; }

void line_free(Line *line) { string_free(&line->text); }

// Waiting for the process to exit (and killing it if it does not) takes up to a
// few seconds: done on a detached "reaper" thread so the UI never freezes.
// line_channel_wait_reapers() (app shutdown) waits until they are all done.
static Mutex g_reaper_lock;
static OnceFlag g_reaper_once = ONCE_FLAG_INITIALIZER;
static i32 g_reapers;

static void reaper_init(void) { mutex_init(&g_reaper_lock); }

static void channel_finish(LineChannel *c) {
    if (!process_wait(c->process, 1500, NULL)) {
        process_kill(c->process);
        process_wait(c->process, 2000, NULL);
    }
    thread_join(&c->stdout_thread);
    thread_join(&c->stderr_thread);
    process_destroy(c->process);
    for (isize i = c->queue_head; i < c->queue.len; ++i) line_free(&c->queue.items[i]);
    vec_free(&c->queue);
    mutex_destroy(&c->lock);
    free(c);
}

static i32 reaper_main(void *arg) {
    channel_finish(arg);
    mutex_lock(&g_reaper_lock);
    g_reapers--;
    mutex_unlock(&g_reaper_lock);
    return 0;
}

void line_channel_stop(LineChannel *c, StringView farewell) {
    if (!c) return;
    if (c->alive && farewell.len) line_channel_send(c, farewell);
    process_close_stdin(c->process);   // most engines quit on EOF right away
    run_once(&g_reaper_once, reaper_init);
    mutex_lock(&g_reaper_lock);
    g_reapers++;
    mutex_unlock(&g_reaper_lock);
    Thread reaper = { 0 };
    if (thread_start(&reaper, reaper_main, c)) {
        thread_detach(&reaper);
    } else {
        reaper_main(c);   // no thread: finish here
    }
}

void line_channel_wait_reapers(i32 timeout_ms) {
    run_once(&g_reaper_once, reaper_init);
    for (i32 waited = 0; waited < timeout_ms; waited += 10) {
        mutex_lock(&g_reaper_lock);
        i32 n = g_reapers;
        mutex_unlock(&g_reaper_lock);
        if (n == 0) return;
        os_sleep_ms(10);
    }
    log_warn("line channel: %d engine process(es) still shutting down", g_reapers);
}
