#include "config.h"

#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <thread>
#include <mutex>
#include <cctype>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/select.h>
#endif

#include "Utils.h"
#include "GTP.h"

Utils::ThreadPool thread_pool;

std::string Utils::format(const char *fmt, ...) {
    va_list ap, copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int n = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) vsnprintf(&out[0], out.size() + 1, fmt, ap);
    va_end(ap);
    return out;
}

bool Utils::starts_with(const std::string & s, const std::string & prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

bool Utils::contains(const std::string & s, const std::string & part) {
    return s.find(part) != std::string::npos;
}

bool Utils::icontains(const std::string & s, const std::string & part) {
    auto lower = [](std::string t) {
        for (auto & c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    return contains(lower(s), lower(part));
}

void Utils::trim(std::string & s) {
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!s.empty() && space(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && space(s[i])) ++i;
    s.erase(0, i);
}

bool Utils::input_causes_stop() {
    return true;
}

bool Utils::input_pending(void) {
#ifndef _WIN32
    fd_set read_fds;
    struct timeval timeout;
    FD_ZERO(&read_fds);
    FD_SET(0,&read_fds);
    timeout.tv_sec = timeout.tv_usec = 0;
    select(1,&read_fds,NULL,NULL,&timeout);
    if (FD_ISSET(0,&read_fds)) {
        return input_causes_stop();
    } else {
        return false;
    }
#else
    static int init = 0, pipe;
    static HANDLE inh;
    DWORD dw;

    if (!init) {
        init = 1;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe) {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }

    if (pipe) {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL)) {
            myprintf("Nothing at other end - exiting\n");
            exit(EXIT_FAILURE);
        }

        if (dw) {
            return input_causes_stop();
        } else {
            return false;
        }
    } else {
        if (!GetNumberOfConsoleInputEvents(inh, &dw)) {
            myprintf("Nothing at other end - exiting\n");
            exit(EXIT_FAILURE);
        }

        if (dw <= 1) {
            return false;
        } else {
            return input_causes_stop();
        }
    }
#endif
    return false;
}

std::mutex IOmutex;

void Utils::GUIAnalysis(void* data) {
}

void Utils::GUIBestMoves(void* data) {
}

void Utils::GUIprintf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    va_end(ap);
}

void Utils::myprintf(const char *fmt, ...) {
    if (cfg_quiet) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (cfg_logfile_handle) {
        std::lock_guard<std::mutex> lock(IOmutex);
        va_start(ap, fmt);
        vfprintf(cfg_logfile_handle, fmt, ap);
        va_end(ap);
    }
}

void Utils::gtp_printf(int id, const char *fmt, ...) {
    va_list ap;

    if (id != -1) {
        fprintf(stdout, "=%d ", id);
    } else {
        fprintf(stdout, "= ");
    }

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    printf("\n\n");

    if (cfg_logfile_handle) {
        std::lock_guard<std::mutex> lock(IOmutex);
        if (id != -1) {
            fprintf(cfg_logfile_handle, "=%d ", id);
        } else {
            fprintf(cfg_logfile_handle, "= ");
        }
        va_start(ap, fmt);
        vfprintf(cfg_logfile_handle, fmt, ap);
        va_end(ap);
        fprintf(cfg_logfile_handle, "\n\n");
    }
}

void Utils::gtp_fail_printf(int id, const char *fmt, ...) {
    va_list ap;

    if (id != -1) {
        fprintf(stdout, "?%d ", id);
    } else {
        fprintf(stdout, "? ");
    }

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    printf("\n\n");

    if (cfg_logfile_handle) {
        std::lock_guard<std::mutex> lock(IOmutex);
        if (id != -1) {
            fprintf(cfg_logfile_handle, "?%d ", id);
        } else {
            fprintf(cfg_logfile_handle, "? ");
        }
        va_start(ap, fmt);
        vfprintf(cfg_logfile_handle, fmt, ap);
        va_end(ap);
        fprintf(cfg_logfile_handle, "\n\n");
    }
}

void Utils::log_input(std::string input) {
    if (cfg_logfile_handle) {
        std::lock_guard<std::mutex> lock(IOmutex);
        fprintf(cfg_logfile_handle, ">>%s\n", input.c_str());
    }
}
