#include "test.h"

#include "base/image.h"
#include "platform/os.h"

#include <string.h>

static void check_pixel(const RgbaImage *im, i32 x, i32 y, u8 r, u8 g, u8 b, u8 a) {
    const u8 *p = im->pixels + ((usize)y * (usize)im->width + (usize)x) * 4;
    CHECK(p[0] == r && p[1] == g && p[2] == b && p[3] == a);
}

static bool load(const char *name, RgbaImage *out) {
    String path = path_join(SV(GIFU_SOURCE_DIR), sv(name));
    String bytes = { 0 };
    bool ok = os_read_file(string_sv(&path), &bytes);
    const char *err = NULL;
    ok = ok && png_decode(string_sv(&bytes), out, &err);
    string_free(&bytes);
    string_free(&path);
    return ok;
}

static void test_png_decode(void) {
    // Made with Python's zlib: 5x5 RGB, row y uses filter type y (none, sub, up, average, paeth).
    RgbaImage im;
    CHECK(load("tests/data/filters.png", &im));
    CHECK(im.width == 5 && im.height == 5);
    for (i32 y = 0; y < 5 && im.pixels; ++y) {
        for (i32 x = 0; x < 5; ++x) check_pixel(&im, x, y, (u8)(x * 50 + y * 7), (u8)(y * 60 + 3), (u8)(x * y * 13), 255);
    }
    rgba_image_free(&im);
    // Palette with transparency.
    CHECK(load("tests/data/palette.png", &im));
    if (im.pixels) {
        check_pixel(&im, 0, 0, 255, 0, 0, 128);
        check_pixel(&im, 2, 0, 0, 0, 255, 255);
        check_pixel(&im, 1, 1, 0, 255, 0, 255);
    }
    rgba_image_free(&im);
    const char *err = NULL;
    CHECK(!png_decode(SV("GIF89a"), &im, &err) && err);
}

static void test_png_roundtrip(void) {
    RgbaImage im;
    CHECK(rgba_image_alloc(&im, 300, 250));   // > 64 KiB of scanlines: several stored blocks
    for (i32 i = 0; i < 300 * 250 * 4; ++i) im.pixels[i] = (u8)(i * 7 + i / 1000);
    String png = { 0 };
    png_encode(&im, &png);
    RgbaImage back;
    const char *err = NULL;
    CHECK(png_decode(string_sv(&png), &back, &err));
    CHECK(back.width == 300 && back.height == 250);
    CHECK(back.pixels && memcmp(back.pixels, im.pixels, 300 * 250 * 4) == 0);
    RgbaImage crop;
    CHECK(rgba_image_crop(&im, 290, -5, 20, 10, &crop));   // clipped to 10 x 5
    CHECK(crop.width == 10 && crop.height == 5);
    CHECK(crop.pixels && memcmp(crop.pixels, im.pixels + 290 * 4, 40) == 0);
    rgba_image_free(&crop);
    rgba_image_free(&back);
    rgba_image_free(&im);
    string_free(&png);
}

void register_image_tests(void) {
    test_register("image/png_decode", test_png_decode);
    test_register("image/png_roundtrip", test_png_roundtrip);
}
