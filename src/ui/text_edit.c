#include "ui/text_edit.h"

#include <string.h>

isize utf8_next_boundary(StringView s, isize i) {
    if (i >= s.len) return s.len;
    ++i;
    while (i < s.len && ((u8)s.data[i] & 0xC0) == 0x80) ++i;
    return i;
}

isize utf8_prev_boundary(StringView s, isize i) {
    if (i <= 0) return 0;
    --i;
    while (i > 0 && ((u8)s.data[i] & 0xC0) == 0x80) --i;
    return i;
}

void text_edit_set(TextEdit *e, StringView text) {
    string_clear(&e->text);
    string_push(&e->text, text);
    e->caret = e->anchor = e->text.len;
}

void text_edit_free(TextEdit *e) { string_free(&e->text); }

bool text_edit_has_selection(const TextEdit *e) { return e->caret != e->anchor; }

StringView text_edit_selection(const TextEdit *e) {
    isize a = MIN(e->caret, e->anchor), b = MAX(e->caret, e->anchor);
    return sv_from_parts(e->text.data + a, b - a);
}

void text_edit_delete_selection(TextEdit *e) {
    isize a = MIN(e->caret, e->anchor), b = MAX(e->caret, e->anchor);
    if (a == b) return;
    memmove(e->text.data + a, e->text.data + b, (usize)(e->text.len - b));
    e->text.len -= b - a;
    e->text.data[e->text.len] = '\0';
    e->caret = e->anchor = a;
}

void text_edit_insert(TextEdit *e, StringView s) {
    text_edit_delete_selection(e);
    // Clean first (CR never, LF only in multi-line edits).
    String clean = { 0 };
    for (isize i = 0; i < s.len; ++i) {
        if (s.data[i] == '\r' || (s.data[i] == '\n' && !e->multiline)) continue;
        string_push_char(&clean, s.data[i]);
    }
    string_reserve(&e->text, e->text.len + clean.len);
    memmove(e->text.data + e->caret + clean.len, e->text.data + e->caret, (usize)(e->text.len - e->caret));
    if (clean.len) memcpy(e->text.data + e->caret, clean.data, (usize)clean.len);
    e->text.len += clean.len;
    if (e->text.data) e->text.data[e->text.len] = '\0';
    e->caret += clean.len;
    e->anchor = e->caret;
    string_free(&clean);
}

void text_edit_backspace(TextEdit *e, bool to_start) {
    if (!text_edit_has_selection(e)) e->anchor = to_start ? 0 : utf8_prev_boundary(string_sv(&e->text), e->caret);
    text_edit_delete_selection(e);
}

void text_edit_delete_forward(TextEdit *e) {
    if (!text_edit_has_selection(e)) e->anchor = utf8_next_boundary(string_sv(&e->text), e->caret);
    text_edit_delete_selection(e);
}

void text_edit_move_to(TextEdit *e, isize caret, bool extend) {
    e->caret = CLAMP(caret, 0, e->text.len);
    if (!extend) e->anchor = e->caret;
}

void text_edit_left(TextEdit *e, bool extend) {
    if (text_edit_has_selection(e) && !extend) text_edit_move_to(e, MIN(e->caret, e->anchor), false);
    else text_edit_move_to(e, utf8_prev_boundary(string_sv(&e->text), e->caret), extend);
}

void text_edit_right(TextEdit *e, bool extend) {
    if (text_edit_has_selection(e) && !extend) text_edit_move_to(e, MAX(e->caret, e->anchor), false);
    else text_edit_move_to(e, utf8_next_boundary(string_sv(&e->text), e->caret), extend);
}

void text_edit_select_all(TextEdit *e) {
    e->anchor = 0;
    e->caret = e->text.len;
}
