// area_select.h - on-screen board area picker (its own process; see area_select.c).
#pragma once

#include "base/base.h"
#include "base/image.h"

// snapshot: the screen under the rectangle, shown frozen behind the box (NULL: a
// transparent window over the live screen).
int area_select_run(i32 x, i32 y, i32 width, i32 height, const char *hint, const RgbaImage *snapshot);
