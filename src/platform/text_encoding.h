// text_encoding.h - legacy charset -> UTF-8 conversion for imported files.
// Uses the OS converters (iconv on POSIX, code pages on Windows).
#pragma once

#include "base/str.h"

bool utf8_is_valid(StringView text);
// Converts `input` in `charset` (e.g. "GB2312", "GBK", "GB18030", "Big5",
// "Shift_JIS", "EUC-KR", "EUC-JP", "ISO-8859-1") to UTF-8. Unknown charsets
// and conversion errors return false.
NODISCARD bool text_to_utf8(StringView charset, StringView input, String *out);
