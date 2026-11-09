#include "base/log.h"

#include <stdarg.h>
#include <stdio.h>

static LogLevel g_min_level = LOG_LEVEL_INFO;

void log_set_level(LogLevel level) { g_min_level = level; }

void log_message(LogLevel level, const char *fmt, ...) {
    if (level < g_min_level) return;
    static const char *const names[] = { "debug", "info", "warn", "error" };
    fprintf(stderr, "[%s] ", names[level]);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}
