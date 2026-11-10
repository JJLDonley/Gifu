// image.h - RGBA images and PNG reading / writing (screen capture, board reading).
//
// The PNG decoder covers what screenshot tools write: 8-bit greyscale, grey +
// alpha, RGB, RGBA and palette images, non-interlaced, every filter type. The
// encoder writes stored (uncompressed) deflate blocks: valid PNG, no compressor
// needed; it is only used for debugging output.
#pragma once

#include "base/str.h"

typedef struct RgbaImage {
    i32 width, height;
    u8 *pixels;   // width * height * 4 bytes, rows top to bottom (heap; rgba_image_free)
} RgbaImage;

bool rgba_image_alloc(RgbaImage *image, i32 width, i32 height);   // zeroed
void rgba_image_free(RgbaImage *image);
// Copies the rectangle (clipped to the image) into `out`.
bool rgba_image_crop(const RgbaImage *image, i32 x, i32 y, i32 width, i32 height, RgbaImage *out);

NODISCARD bool png_decode(StringView bytes, RgbaImage *out, const char **error);
void png_encode(const RgbaImage *image, String *out);
