// text_edit.h - the editing model behind text inputs (no raylib): a UTF-8
// buffer with a caret and a selection anchor. text_input.c maps keys and the
// mouse onto these operations; they are unit-tested on their own.
#pragma once

#include "base/str.h"

typedef struct TextEdit {
    String text;
    isize caret, anchor;   // byte offsets; anchor == caret means no selection
    bool multiline;
    f64 blink_start;
} TextEdit;

void text_edit_set(TextEdit *edit, StringView text);
void text_edit_free(TextEdit *edit);

bool text_edit_has_selection(const TextEdit *edit);
StringView text_edit_selection(const TextEdit *edit);
void text_edit_delete_selection(TextEdit *edit);
// Replaces the selection with `s` (newlines dropped on single-line edits, CR always).
void text_edit_insert(TextEdit *edit, StringView s);
// Backspace / Delete: one character (code point), or to the start (`to_start`).
void text_edit_backspace(TextEdit *edit, bool to_start);
void text_edit_delete_forward(TextEdit *edit);
// Moves the caret; `extend` keeps the anchor (Shift).
void text_edit_move_to(TextEdit *edit, isize caret, bool extend);
void text_edit_left(TextEdit *edit, bool extend);    // collapses a selection to its start without Shift
void text_edit_right(TextEdit *edit, bool extend);
void text_edit_select_all(TextEdit *edit);

isize utf8_next_boundary(StringView s, isize i);
isize utf8_prev_boundary(StringView s, isize i);
