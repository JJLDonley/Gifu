// area_select.h - on-screen board area picker (its own process; see area_select.c).
#pragma once

#include "base/base.h"

int area_select_run(i32 x, i32 y, i32 width, i32 height, const char *hint);
