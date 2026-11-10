#include "base/image.h"

#include "base/codec.h"

#include <stdlib.h>
#include <string.h>

int catagui_sinflate(void *out, int cap, const void *in, int size);   // third_party/sinfl (renamed)

bool rgba_image_alloc(RgbaImage *image, i32 width, i32 height) {
    *image = (RgbaImage){ 0 };
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) return false;
    image->pixels = calloc((usize)width * (usize)height, 4);
    if (!image->pixels) return false;
    image->width = width, image->height = height;
    return true;
}

void rgba_image_free(RgbaImage *image) {
    free(image->pixels);
    *image = (RgbaImage){ 0 };
}

bool rgba_image_crop(const RgbaImage *image, i32 x, i32 y, i32 width, i32 height, RgbaImage *out) {
    i32 x0 = MAX(x, 0), y0 = MAX(y, 0), x1 = MIN(x + width, image->width), y1 = MIN(y + height, image->height);
    if (x1 <= x0 || y1 <= y0 || !rgba_image_alloc(out, x1 - x0, y1 - y0)) return false;
    for (i32 r = y0; r < y1; ++r) {
        memcpy(out->pixels + (usize)(r - y0) * (usize)out->width * 4, image->pixels + ((usize)r * (usize)image->width + (usize)x0) * 4,
               (usize)out->width * 4);
    }
    return true;
}

// ---- Decoding ---------------------------------------------------------------------------------------

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static u8 paeth(u8 a, u8 b, u8 c) {
    i32 p = (i32)a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

bool png_decode(StringView bytes, RgbaImage *out, const char **error) {
    const char *dummy;
    if (!error) error = &dummy;
    *out = (RgbaImage){ 0 };
    static const u8 SIGNATURE[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    const u8 *p = (const u8 *)bytes.data;
    if (bytes.len < 8 || memcmp(p, SIGNATURE, 8) != 0) {
        *error = "not a PNG file";
        return false;
    }
    i32 width = 0, height = 0, depth = 0, type = -1, interlace = 0;
    u8 palette[256 * 4];
    memset(palette, 255, sizeof(palette));
    String idat = { 0 };
    bool ended = false;
    for (isize pos = 8; pos + 12 <= bytes.len && !ended;) {
        u32 len = be32(p + pos);
        if ((isize)len > bytes.len - pos - 12) break;
        const u8 *tag = p + pos + 4, *data = p + pos + 8;
        if (memcmp(tag, "IHDR", 4) == 0 && len >= 13) {
            width = (i32)be32(data), height = (i32)be32(data + 4), depth = data[8], type = data[9], interlace = data[12];
        } else if (memcmp(tag, "PLTE", 4) == 0) {
            for (u32 i = 0; i < len / 3 && i < 256; ++i) memcpy(palette + i * 4, data + i * 3, 3);
        } else if (memcmp(tag, "tRNS", 4) == 0 && type == 3) {
            for (u32 i = 0; i < len && i < 256; ++i) palette[i * 4 + 3] = data[i];
        } else if (memcmp(tag, "IDAT", 4) == 0) {
            string_push(&idat, sv_from_parts((const char *)data, len));
        } else if (memcmp(tag, "IEND", 4) == 0) {
            ended = true;
        }
        pos += 12 + (isize)len;
    }
    static const i32 CHANNELS[7] = { 1, 0, 3, 1, 2, 0, 4 };
    if (width <= 0 || height <= 0 || type < 0 || type > 6 || CHANNELS[type] == 0) {
        *error = "unsupported or damaged PNG header";
    } else if (depth != 8 || interlace != 0) {
        *error = "only 8-bit, non-interlaced PNG images are supported";
    } else if (idat.len < 6) {
        *error = "PNG has no image data";
    } else {
        *error = NULL;
    }
    if (*error) {
        string_free(&idat);
        return false;
    }
    i32 channels = CHANNELS[type];
    usize stride = (usize)width * (usize)channels, raw_size = (stride + 1) * (usize)height;
    u8 *raw = malloc(raw_size);
    if (!raw) abort();
    // zlib: 2-byte header, raw deflate, adler32 (not checked: chunk CRCs already passed through the tool that wrote it).
    int got = catagui_sinflate(raw, (int)raw_size, idat.data + 2, (int)(idat.len - 2));
    string_free(&idat);
    if (got != (int)raw_size || !rgba_image_alloc(out, width, height)) {
        free(raw);
        *error = "PNG image data is damaged";
        return false;
    }
    // Unfilter in place (each row: filter byte + stride bytes).
    for (i32 y = 0; y < height; ++y) {
        u8 *row = raw + (usize)y * (stride + 1) + 1, *prev = y ? row - (stride + 1) : NULL;
        u8 filter = row[-1];
        for (usize i = 0; i < stride; ++i) {
            u8 a = i >= (usize)channels ? row[i - (usize)channels] : 0, b = prev ? prev[i] : 0;
            u8 c = prev && i >= (usize)channels ? prev[i - (usize)channels] : 0;
            switch (filter) {
                case 1: row[i] = (u8)(row[i] + a); break;
                case 2: row[i] = (u8)(row[i] + b); break;
                case 3: row[i] = (u8)(row[i] + ((a + b) >> 1)); break;
                case 4: row[i] = (u8)(row[i] + paeth(a, b, c)); break;
                default: break;
            }
        }
        u8 *dst = out->pixels + (usize)y * (usize)width * 4;
        for (i32 x = 0; x < width; ++x, dst += 4) {
            const u8 *s = row + (usize)x * (usize)channels;
            switch (type) {
                case 0: dst[0] = dst[1] = dst[2] = s[0], dst[3] = 255; break;
                case 2: dst[0] = s[0], dst[1] = s[1], dst[2] = s[2], dst[3] = 255; break;
                case 3: memcpy(dst, palette + s[0] * 4, 4); break;
                case 4: dst[0] = dst[1] = dst[2] = s[0], dst[3] = s[1]; break;
                case 6: memcpy(dst, s, 4); break;
                default: break;
            }
        }
    }
    free(raw);
    return true;
}

// ---- Encoding (stored deflate) ----------------------------------------------------------------------

static void push_be32(String *out, u32 v) {
    char b[4] = { (char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v };
    string_push(out, sv_from_parts(b, 4));
}

static void push_chunk(String *out, const char *tag, StringView data) {
    push_be32(out, (u32)data.len);
    isize start = out->len;
    string_push(out, sv_from_parts(tag, 4));
    string_push(out, data);
    push_be32(out, crc32_update(0, sv_from_parts(out->data + start, out->len - start)));
}

void png_encode(const RgbaImage *image, String *out) {
    string_push(out, sv_from_parts("\x89PNG\r\n\x1a\n", 8));
    u8 ihdr[13] = { 0 };
    for (i32 i = 0; i < 4; ++i) ihdr[i] = (u8)(image->width >> (24 - 8 * i)), ihdr[4 + i] = (u8)(image->height >> (24 - 8 * i));
    ihdr[8] = 8, ihdr[9] = 6;
    push_chunk(out, "IHDR", sv_from_parts((const char *)ihdr, 13));
    // Raw scanlines (filter 0), then zlib with stored blocks of at most 65535 bytes.
    usize stride = (usize)image->width * 4, raw_size = (stride + 1) * (usize)image->height;
    String z = string_from(sv_from_parts("\x78\x01", 2));
    u32 s1 = 1, s2 = 0;   // adler32
    usize done = 0;
    char *block = malloc(65535);
    if (!block) abort();
    while (done < raw_size || raw_size == 0) {
        usize n = MIN(raw_size - done, (usize)65535);
        for (usize i = 0; i < n; ++i) {
            usize k = done + i, row = k / (stride + 1), col = k % (stride + 1);
            u8 v = col == 0 ? 0 : image->pixels[row * stride + col - 1];
            block[i] = (char)v;
            s1 = (s1 + v) % 65521, s2 = (s2 + s1) % 65521;
        }
        done += n;
        char head[5] = { (char)(done >= raw_size), (char)(n & 255), (char)(n >> 8), (char)(~n & 255), (char)((~n >> 8) & 255) };
        string_push(&z, sv_from_parts(head, 5));
        string_push(&z, sv_from_parts(block, (isize)n));
        if (raw_size == 0) break;
    }
    free(block);
    push_be32(&z, s2 << 16 | s1);
    push_chunk(out, "IDAT", string_sv(&z));
    push_chunk(out, "IEND", SV(""));
    string_free(&z);
}
