// Single translation unit for header-only third-party libraries.
#define CLAY_IMPLEMENTATION
#include "clay.h"

// Our own private copy of stb_truetype (raylib compiles its copy as static).
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
