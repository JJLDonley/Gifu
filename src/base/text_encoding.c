#include "base/text_encoding.h"

#include <stdlib.h>
#include <string.h>

bool utf8_is_valid(StringView text) {
    const u8 *p = (const u8 *)text.data;
    isize i = 0, n = text.len;
    while (i < n) {
        u8 c = p[i];
        isize len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (len == 0 || i + len > n) return false;
        if (len == 2 && c < 0xC2) return false;   // overlong
        for (isize k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
        }
        i += len;
    }
    return true;
}

// Canonical charset family for a label (case/punctuation-insensitive).
typedef enum Charset { CS_UNKNOWN, CS_UTF8, CS_GB18030, CS_BIG5, CS_SHIFT_JIS, CS_EUC_JP, CS_EUC_KR, CS_LATIN1 } Charset;

static Charset charset_from_label(StringView label) {
    char norm[32];
    i32 n = 0;
    for (isize i = 0; i < label.len && n < (i32)sizeof(norm) - 1; ++i) {
        char c = label.data[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) norm[n++] = c;
    }
    norm[n] = '\0';
    StringView s = sv(norm);
    if (sv_eq(s, SV("utf8"))) return CS_UTF8;
    if (sv_eq(s, SV("gb2312")) || sv_eq(s, SV("gbk")) || sv_eq(s, SV("gb18030")) || sv_eq(s, SV("cp936")) ||
        sv_eq(s, SV("euccn")))
        return CS_GB18030;
    if (sv_eq(s, SV("big5")) || sv_eq(s, SV("big5hkscs")) || sv_eq(s, SV("cp950"))) return CS_BIG5;
    if (sv_eq(s, SV("shiftjis")) || sv_eq(s, SV("sjis")) || sv_eq(s, SV("cp932")) || sv_eq(s, SV("windows31j")))
        return CS_SHIFT_JIS;
    if (sv_eq(s, SV("eucjp"))) return CS_EUC_JP;
    if (sv_eq(s, SV("euckr")) || sv_eq(s, SV("cp949")) || sv_eq(s, SV("ksc56011987")) || sv_eq(s, SV("uhc")))
        return CS_EUC_KR;
    if (sv_eq(s, SV("iso88591")) || sv_eq(s, SV("latin1")) || sv_eq(s, SV("cp1252")) || sv_eq(s, SV("windows1252")))
        return CS_LATIN1;
    return CS_UNKNOWN;
}

#if defined(OS_WINDOWS)
#include <windows.h>

static UINT code_page(Charset cs) {
    switch (cs) {
        case CS_GB18030: return 54936;
        case CS_BIG5: return 950;
        case CS_SHIFT_JIS: return 932;
        case CS_EUC_JP: return 20932;
        case CS_EUC_KR: return 949;
        case CS_LATIN1: return 1252;
        default: return 0;
    }
}

bool text_to_utf8(StringView charset, StringView input, String *out) {
    Charset cs = charset_from_label(charset);
    if (cs == CS_UTF8) {
        *out = string_from(input);
        return utf8_is_valid(input);
    }
    UINT cp = code_page(cs);
    if (!cp) return false;
    if (input.len == 0) {
        *out = string_from(SV(""));
        return true;
    }
    int wide_len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, input.data, (int)input.len, NULL, 0);
    if (wide_len <= 0) wide_len = MultiByteToWideChar(cp, 0, input.data, (int)input.len, NULL, 0);   // lenient
    if (wide_len <= 0) return false;
    wchar_t *wide = malloc(sizeof(wchar_t) * (usize)wide_len);
    if (!wide) return false;
    MultiByteToWideChar(cp, 0, input.data, (int)input.len, wide, wide_len);
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL);
    String result = { 0 };
    string_reserve(&result, len);
    WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, result.data, len, NULL, NULL);
    result.len = len;
    result.data[len] = '\0';
    free(wide);
    *out = result;
    return true;
}

#else
#include <errno.h>
#include <iconv.h>

static const char *iconv_name(Charset cs) {
    switch (cs) {
        case CS_GB18030: return "GB18030";
        case CS_BIG5: return "BIG5-HKSCS";
        case CS_SHIFT_JIS: return "CP932";
        case CS_EUC_JP: return "EUC-JP";
        case CS_EUC_KR: return "CP949";
        case CS_LATIN1: return "CP1252";
        default: return NULL;
    }
}

bool text_to_utf8(StringView charset, StringView input, String *out) {
    Charset cs = charset_from_label(charset);
    if (cs == CS_UTF8) {
        *out = string_from(input);
        return utf8_is_valid(input);
    }
    const char *name = iconv_name(cs);
    if (!name) return false;
    iconv_t cd = iconv_open("UTF-8", name);
    if (cd == (iconv_t)-1) return false;
    String result = { 0 };
    string_reserve(&result, input.len * 2 + 16);
    char *in = (char *)input.data;
    size_t in_left = (size_t)input.len;
    bool ok = true;
    while (in_left > 0) {
        char *dst = result.data + result.len;
        size_t out_left = (size_t)(result.cap - 1 - result.len);
        size_t rc = iconv(cd, &in, &in_left, &dst, &out_left);
        result.len = dst - result.data;
        if (rc != (size_t)-1) continue;
        if (errno == E2BIG) {
            string_reserve(&result, result.cap * 2);
        } else if (errno == EILSEQ || errno == EINVAL) {
            // Skip one bad byte, emit U+FFFD, keep going (damaged files are common).
            string_reserve(&result, result.len + 4);
            memcpy(result.data + result.len, "\xEF\xBF\xBD", 3);
            result.len += 3;
            in++;
            in_left--;
        } else {
            ok = false;
            break;
        }
    }
    iconv_close(cd);
    result.data[result.len] = '\0';
    if (!ok) {
        string_free(&result);
        return false;
    }
    *out = result;
    return true;
}
#endif
