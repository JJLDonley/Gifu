// codec.h - small byte codecs for interchange formats (KaTrain's KT[] analysis).
#pragma once

#include "base/str.h"

void base64_encode(StringView bytes, String *out);                 // standard alphabet, padded
NODISCARD bool base64_decode(StringView text, String *out);        // ignores whitespace
u32 crc32_update(u32 crc, StringView bytes);                       // start with 0

// SHA-256 (FIPS 180-4), streaming; hex digests are lowercase.
typedef struct Sha256 {
    u32 state[8];
    u64 length;        // bytes so far
    u8 block[64];
    i32 used;
} Sha256;
void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, StringView bytes);
void sha256_final(Sha256 *s, u8 digest[32]);
void sha256_hex(StringView bytes, char out[65]);
NODISCARD bool sha256_file_hex(StringView path, char out[65]);   // false if unreadable
// gzip (RFC 1952). Writing uses stored deflate blocks (no compression): valid
// for every reader, and the payloads here are small.
void gzip_compress_stored(StringView bytes, String *out);
NODISCARD bool gzip_decompress(StringView gz, String *out);
