#include "core/go.h"

#include <ctype.h>
#include <stdio.h>

static const char GTP_COLUMNS[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";

bool point_from_gtp(StringView text, i32 width, i32 height, Point *out) {
    text = sv_trim(text);
    if (sv_eq_nocase(text, SV("pass"))) {
        *out = POINT_PASS;
        return true;
    }
    if (text.len < 2) return false;
    char column = (char)toupper((u8)text.data[0]);
    i32 x = -1;
    for (i32 i = 0; GTP_COLUMNS[i]; ++i) {
        if (GTP_COLUMNS[i] == column) x = i;
    }
    i64 row = 0;
    if (x < 0 || x >= width || !sv_parse_i64(sv_drop(text, 1), &row) || row < 1 || row > height) return false;
    *out = (Point){ (i8)x, (i8)(height - row) };
    return true;
}

i32 point_to_gtp(Point p, i32 width, i32 height, char *buffer) {
    UNUSED(width);
    if (point_is_pass(p)) return snprintf(buffer, 8, "pass");
    return snprintf(buffer, 8, "%c%d", GTP_COLUMNS[p.x], height - p.y);
}

bool point_from_sgf(StringView text, i32 width, i32 height, Point *out) {
    text = sv_trim(text);
    if (text.len == 0 || (sv_eq(text, SV("tt")) && width <= 19 && height <= 19)) {
        *out = POINT_PASS;
        return true;
    }
    if (text.len != 2) return false;
    i32 x = text.data[0] - 'a';
    i32 y = text.data[1] - 'a';
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    *out = (Point){ (i8)x, (i8)y };
    return true;
}

i32 point_to_sgf(Point p, char *buffer) {
    if (point_is_pass(p)) {
        buffer[0] = '\0';
        return 0;
    }
    buffer[0] = (char)('a' + p.x);
    buffer[1] = (char)('a' + p.y);
    buffer[2] = '\0';
    return 2;
}
