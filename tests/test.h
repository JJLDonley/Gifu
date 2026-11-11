// test.h - minimal unit test harness (no dependencies).
#pragma once

#include "base/base.h"
#include "base/str.h"

#include <stdio.h>

typedef void (*TestFn)(void);

void test_register(const char *name, TestFn fn);
void test_fail(const char *file, int line, const char *message);

#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond)) test_fail(__FILE__, __LINE__, #cond);  \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        long long _a = (long long)(a), _b = (long long)(b);                             \
        if (_a != _b) {                                                                 \
            char _msg[256];                                                             \
            snprintf(_msg, sizeof(_msg), "%s == %s (%lld vs %lld)", #a, #b, _a, _b);    \
            test_fail(__FILE__, __LINE__, _msg);                                        \
        }                                                                               \
    } while (0)

#define CHECK_SV(a, b)                                                                              \
    do {                                                                                            \
        StringView _a = (a), _b = (b);                                                              \
        if (!sv_eq(_a, _b)) {                                                                       \
            char _msg[512];                                                                         \
            snprintf(_msg, sizeof(_msg), "%s == %s (\"%.*s\" vs \"%.*s\")", #a, #b, SV_ARG(_a), SV_ARG(_b)); \
            test_fail(__FILE__, __LINE__, _msg);                                                    \
        }                                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(((a) - (b)) < (eps) && ((b) - (a)) < (eps))
