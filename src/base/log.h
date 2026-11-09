// log.h - leveled logging to stderr (and optionally a file).
#pragma once

#include "base/base.h"

typedef enum LogLevel { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR } LogLevel;

void log_set_level(LogLevel level);
void log_message(LogLevel level, const char *fmt, ...) PRINTF_LIKE(2, 3);

#define log_debug(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)
