// codec.h - small byte codecs for interchange formats (KaTrain's KT[] analysis).
#pragma once

#include "base/str.h"

void base64_encode(StringView bytes, String *out);                 // standard alphabet, padded
NODISCARD bool base64_decode(StringView text, String *out);        // ignores whitespace
u32 crc32_update(u32 crc, StringView bytes);                       // start with 0
// gzip (RFC 1952). Writing uses stored deflate blocks (no compression): valid
// for every reader, and the payloads here are small.
void gzip_compress_stored(StringView bytes, String *out);
NODISCARD bool gzip_decompress(StringView gz, String *out);
