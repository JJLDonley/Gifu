#include "base/console.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct {
    atomic_flag lock;
    ConsoleLine lines[CONSOLE_CAPACITY];
    i64 last;            // newest seq
    struct timespec start;
    bool started;
} g = { .lock = ATOMIC_FLAG_INIT };
static atomic_bool g_capture_analysis;

static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&g.lock, memory_order_acquire)) {}
}
static void unlock(void) { atomic_flag_clear_explicit(&g.lock, memory_order_release); }

static f64 now(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    if (!g.started) g.start = t, g.started = true;
    return (f64)(t.tv_sec - g.start.tv_sec) + (f64)(t.tv_nsec - g.start.tv_nsec) * 1e-9;
}

void console_add(ConsoleKind kind, StringView source, StringView text) {
    if (kind == CONSOLE_ANALYSIS && !atomic_load(&g_capture_analysis)) return;
    String copy = string_from(text);   // allocated outside the lock
    lock();
    i64 seq = ++g.last;
    ConsoleLine *l = &g.lines[seq % CONSOLE_CAPACITY];
    String old = l->text;
    *l = (ConsoleLine){ .seq = seq, .time = now(), .kind = kind, .text = copy };
    snprintf(l->source, sizeof(l->source), SV_FMT, SV_ARG(sv_take(source, (isize)sizeof(l->source) - 1)));
    unlock();
    string_free(&old);
}

i64 console_last_seq(void) {
    lock();
    i64 v = g.last;
    unlock();
    return v;
}

i64 console_first_seq(void) {
    lock();
    i64 v = g.last >= CONSOLE_CAPACITY ? g.last - CONSOLE_CAPACITY + 1 : 1;
    unlock();
    return v;
}

bool console_get(i64 seq, ConsoleLine *out) {
    bool ok = false;
    lock();
    const ConsoleLine *l = &g.lines[seq % CONSOLE_CAPACITY];
    if (seq > 0 && l->seq == seq) {
        *out = *l;
        out->text = string_from(string_sv(&l->text));   // (a short copy under the lock)
        ok = true;
    }
    unlock();
    return ok;
}

void console_clear(void) {
    lock();
    for (i32 i = 0; i < CONSOLE_CAPACITY; ++i) string_free(&g.lines[i].text), g.lines[i].seq = 0;
    unlock();
}

void console_set_capture_analysis(bool on) { atomic_store(&g_capture_analysis, on); }
bool console_capture_analysis(void) { return atomic_load(&g_capture_analysis); }
