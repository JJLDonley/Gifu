// base.h - fundamental types and macros used everywhere in Gifu.
//
// Conventions:
//   * Fixed-width integer aliases (i8..i64, u8..u64), f32/f64, isize/usize.
//   * Sizes and counts are signed (isize) so subtraction never wraps.
//   * Types are PascalCase, functions are module_verb (snake_case),
//     constants and enum values are UPPER_SNAKE_CASE.
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;
typedef ptrdiff_t isize;
typedef size_t    usize;

#define countof(a) ((isize)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define UNUSED(x) ((void)(x))

#if defined(__GNUC__) || defined(__clang__)
    #define NODISCARD __attribute__((warn_unused_result))
    // gnu_printf on mingw: there plain "printf" means Microsoft's old rules (no %td / %zd),
    // but the UCRT runtime Gifu links against implements C99 formats. Apple's clang
    // does not know gnu_printf.
    #if defined(__MINGW32__)
        #define PRINTF_LIKE(fmt_index, args_index) __attribute__((format(gnu_printf, fmt_index, args_index)))
    #else
        #define PRINTF_LIKE(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
    #endif
    #define FALLTHROUGH __attribute__((fallthrough))
#else
    #define NODISCARD _Check_return_
    #define PRINTF_LIKE(fmt_index, args_index)
    #define FALLTHROUGH ((void)0)
#endif

#define ASSERT(cond) assert(cond)
#define UNREACHABLE() (assert(!"unreachable"), (void)0)

#if defined(_WIN32)
    #define OS_WINDOWS 1
#else
    #define OS_POSIX 1
#endif
