// Single translation unit for header-only third-party libraries.
#define CLAY_IMPLEMENTATION
#include "clay.h"

// Raw-deflate decoder for zip extraction. raylib exports `sinflate` itself, so
// our copy is renamed to avoid duplicate symbols when both are linked.
#define sinflate gifu_sinflate
#define zsinflate gifu_zsinflate
#define SINFL_IMPLEMENTATION
#define SINFL_NO_SIMD
#include "sinfl.h"

// Our own private copy of stb_truetype (raylib compiles its copy as static).
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
