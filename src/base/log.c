#include "base/log.h"

#include <stdarg.h>
#include <stdio.h>

static LogLevel g_min_level = LOG_LEVEL_INFO;
static FILE *g_file;   // also written here when open (log_open_file)

void log_set_level(LogLevel level) { g_min_level = level; }

bool log_open_file(const char *path) {
    if (g_file) fclose(g_file);
    g_file = fopen(path, "w");   // a fresh log each run
    return g_file != NULL;
}

void log_message(LogLevel level, const char *fmt, ...) {
    if (level < g_min_level) return;
    static const char *const names[] = { "debug", "info", "warn", "error" };
    // One formatted line, written with one call per stream (threads log too).
    char line[1024];
    int n = snprintf(line, sizeof(line), "[%s] ", names[level]);
    va_list args;
    va_start(args, fmt);
    if (n > 0 && n < (int)sizeof(line)) vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", line);
    if (g_file) {
        fprintf(g_file, "%s\n", line);
        fflush(g_file);
    }
}
