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

typedef enum TextInputResult {
    TEXT_INPUT_NONE,
    TEXT_INPUT_CHANGED,
    TEXT_INPUT_COMMITTED,   // focus left (click outside, Enter on single-line, Ctrl+Enter)
    TEXT_INPUT_CANCELLED,   // Escape
} TextInputResult;

// The keys of one frame (presses and auto-repeats), gathered by the UI.
typedef struct TextKeys {
    u32 chars[32];   // typed / IME-committed code points, in order
    i32 char_count;
    bool ctrl, shift;
    bool backspace, del, left, right, up, down, home, end, enter, escape;
    bool select_all, copy, cut, paste;   // Ctrl+A / C / X / V
} TextKeys;

// What needs fonts or the system: line moves on the wrapped layout, the clipboard.
typedef struct TextEditHost {
    void *user;
    isize (*vertical)(void *user, const TextEdit *edit, i32 direction);   // caret one visual line up (-1) / down (+1)
    void (*line_bounds)(void *user, const TextEdit *edit, isize caret, isize *start, isize *end);   // visual line of caret
    void (*set_clipboard)(void *user, StringView text);
    StringView (*get_clipboard)(void *user);
} TextEditHost;

// Applies a frame of keys: typing, deleting, caret moves (Shift extends, Ctrl+Home/End
// go to the ends), Ctrl+A/C/X/V, Enter (new line in multi-line edits, Ctrl+Enter or a
// single-line edit commits), Escape (cancels).
TextInputResult text_edit_apply_keys(TextEdit *edit, const TextKeys *keys, const TextEditHost *host);

isize utf8_next_boundary(StringView s, isize i);
isize utf8_prev_boundary(StringView s, isize i);
