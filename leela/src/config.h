#ifndef CONFIG_INCLUDED
#define CONFIG_INCLUDED

#include <cstdint>

/* Features: the build defines USE_OPENCL for the GPU version (leela-opencl);
 * without it the CPU version uses its own matrix routines (USE_BLAS, Gemm.h). */
#define USE_OPTIONS
#define USE_SEARCH
#ifndef USE_OPENCL
#define USE_BLAS
#endif

#ifdef _WIN32
#define NOMINMAX
#endif

#define PROGRAM_NAME "Leela"
#define PROGRAM_VERSION "0.11.0"

#define MAX_CPUS 128

/* Integer types */
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef int64_t int64;
typedef uint64_t uint64;

#endif
