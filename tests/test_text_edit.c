#include "test.h"

#include "ui/text_edit.h"

static void test_text_edit_basics(void) {
    TextEdit e = { 0 };
    text_edit_insert(&e, SV("Hello"));
    CHECK_SV(string_sv(&e.text), SV("Hello"));
    CHECK_EQ(e.caret, 5);
    text_edit_left(&e, false);
    text_edit_left(&e, false);
    text_edit_insert(&e, SV("XY"));
    CHECK_SV(string_sv(&e.text), SV("HelXYlo"));
    text_edit_backspace(&e, false);
    CHECK_SV(string_sv(&e.text), SV("HelXlo"));
    text_edit_delete_forward(&e);
    CHECK_SV(string_sv(&e.text), SV("HelXo"));
    // Shift+Left x2 selects, typing replaces the selection.
    text_edit_left(&e, true);
    text_edit_left(&e, true);
    CHECK(text_edit_has_selection(&e));
    CHECK_SV(text_edit_selection(&e), SV("lX"));
    text_edit_insert(&e, SV("-"));
    CHECK_SV(string_sv(&e.text), SV("He-o"));
    // Left without Shift collapses a selection to its start.
    text_edit_select_all(&e);
    text_edit_left(&e, false);
    CHECK(!text_edit_has_selection(&e));
    CHECK_EQ(e.caret, 0);
    // Ctrl+Backspace: to the start.
    text_edit_move_to(&e, 3, false);
    text_edit_backspace(&e, true);
    CHECK_SV(string_sv(&e.text), SV("o"));
    text_edit_free(&e);
}

static void test_text_edit_utf8(void) {
    TextEdit e = { 0 };
    text_edit_set(&e, SV("a李明b"));   // 1 + 3 + 3 + 1 bytes
    text_edit_left(&e, false);           // before 'b'
    CHECK_EQ(e.caret, 7);
    text_edit_backspace(&e, false);      // removes 明 (3 bytes), never half a character
    CHECK_SV(string_sv(&e.text), SV("a李b"));
    text_edit_move_to(&e, 1, false);
    text_edit_delete_forward(&e);
    CHECK_SV(string_sv(&e.text), SV("ab"));
    text_edit_move_to(&e, 1, false);
    text_edit_right(&e, true);
    CHECK_SV(text_edit_selection(&e), SV("b"));
    text_edit_free(&e);
}

static void test_text_edit_paste(void) {
    TextEdit single = { 0 }, multi = { .multiline = true };
    text_edit_insert(&single, SV("one\r\ntwo"));     // single-line: line breaks dropped
    CHECK_SV(string_sv(&single.text), SV("onetwo"));
    text_edit_insert(&multi, SV("one\r\ntwo"));      // multi-line keeps LF, drops CR
    CHECK_SV(string_sv(&multi.text), SV("one\ntwo"));
    text_edit_select_all(&multi);
    text_edit_delete_selection(&multi);              // cut
    CHECK_EQ(multi.text.len, 0);
    text_edit_move_to(&multi, 99, false);            // clamped
    CHECK_EQ(multi.caret, 0);
    text_edit_free(&single);
    text_edit_free(&multi);
}

// A host whose visual lines are 10 bytes long (ASCII here), with an in-memory clipboard.
typedef struct FakeHost {
    String clipboard;
} FakeHost;
static isize fake_vertical(void *user, const TextEdit *e, i32 direction) {
    UNUSED(user);
    isize target = e->caret + 10 * direction;
    return target < 0 ? 0 : target > e->text.len ? e->text.len : target;
}
static void fake_line_bounds(void *user, const TextEdit *e, isize caret, isize *start, isize *end) {
    UNUSED(user);
    *start = caret / 10 * 10;
    *end = MIN(*start + 10, e->text.len);
}
static void fake_set_clipboard(void *user, StringView text) {
    FakeHost *h = user;
    string_free(&h->clipboard);
    h->clipboard = string_from(text);
}
static StringView fake_get_clipboard(void *user) { return string_sv(&((FakeHost *)user)->clipboard); }

static TextInputResult press(TextEdit *e, FakeHost *h, TextKeys k) {
    TextEditHost host = { h, fake_vertical, fake_line_bounds, fake_set_clipboard, fake_get_clipboard };
    return text_edit_apply_keys(e, &k, &host);
}

static TextKeys typed(const char *ascii) {
    TextKeys k = { 0 };
    for (const char *p = ascii; *p && k.char_count < 32; ++p) k.chars[k.char_count++] = (u8)*p;
    return k;
}

static void test_text_edit_keys(void) {
    FakeHost h = { 0 };
    TextEdit e = { 0 };
    text_edit_set(&e, SV(""));
    // Typing; control characters (Ctrl+A arrives as 0x01 on some platforms) are dropped.
    TextKeys k = typed("abc");
    k.chars[k.char_count++] = 0x01;
    k.chars[k.char_count++] = 0x56FD;   // 国
    CHECK_EQ(press(&e, &h, k), TEXT_INPUT_CHANGED);
    CHECK_SV(string_sv(&e.text), SV("abc\xe5\x9b\xbd"));
    // Shift+Left twice selects the last two characters; typing replaces them.
    CHECK_EQ(press(&e, &h, (TextKeys){ .shift = true, .left = true }), TEXT_INPUT_NONE);
    press(&e, &h, (TextKeys){ .shift = true, .left = true });
    CHECK_SV(text_edit_selection(&e), SV("c\xe5\x9b\xbd"));
    press(&e, &h, typed("Z"));
    CHECK_SV(string_sv(&e.text), SV("abZ"));
    // Home / End stay on the visual line; Ctrl goes to the ends; Shift+End selects.
    text_edit_set(&e, SV("0123456789abcdefghij"));
    text_edit_move_to(&e, 14, false);
    press(&e, &h, (TextKeys){ .home = true });
    CHECK_EQ(e.caret, 10);
    press(&e, &h, (TextKeys){ .shift = true, .end = true });
    CHECK_SV(text_edit_selection(&e), SV("abcdefghij"));
    press(&e, &h, (TextKeys){ .ctrl = true, .home = true });
    CHECK(e.caret == 0 && !text_edit_has_selection(&e));
    // Up / Down through the host (one visual line = 10 here), clamped at the ends.
    press(&e, &h, (TextKeys){ .down = true });
    CHECK_EQ(e.caret, 10);
    press(&e, &h, (TextKeys){ .down = true });
    press(&e, &h, (TextKeys){ .down = true });
    CHECK_EQ(e.caret, 20);
    press(&e, &h, (TextKeys){ .up = true, .shift = true });
    CHECK_SV(text_edit_selection(&e), SV("abcdefghij"));
    // Clipboard: copy needs a selection; cut empties; paste puts it back.
    press(&e, &h, (TextKeys){ .ctrl = true, .copy = true });
    CHECK_SV(string_sv(&h.clipboard), SV("abcdefghij"));
    press(&e, &h, (TextKeys){ .ctrl = true, .right = true });   // collapse
    press(&e, &h, (TextKeys){ .ctrl = true, .copy = true });
    CHECK_SV(string_sv(&h.clipboard), SV("abcdefghij"));   // unchanged: nothing selected
    press(&e, &h, (TextKeys){ .ctrl = true, .select_all = true });
    CHECK_EQ(press(&e, &h, (TextKeys){ .ctrl = true, .cut = true }), TEXT_INPUT_CHANGED);
    CHECK_EQ(e.text.len, 0);
    CHECK_SV(string_sv(&h.clipboard), SV("0123456789abcdefghij"));
    CHECK_EQ(press(&e, &h, (TextKeys){ .ctrl = true, .paste = true }), TEXT_INPUT_CHANGED);
    CHECK_SV(string_sv(&e.text), SV("0123456789abcdefghij"));
    // Ctrl+Backspace deletes to the start; a plain one deletes one character.
    text_edit_move_to(&e, 5, false);
    press(&e, &h, (TextKeys){ .ctrl = true, .backspace = true });
    CHECK_SV(string_sv(&e.text), SV("56789abcdefghij"));
    press(&e, &h, (TextKeys){ .del = true });
    CHECK_SV(string_sv(&e.text), SV("6789abcdefghij"));
    // Enter: commits a single-line edit; new line in a multi-line one, Ctrl+Enter commits it.
    CHECK_EQ(press(&e, &h, (TextKeys){ .enter = true }), TEXT_INPUT_COMMITTED);
    e.multiline = true;
    CHECK_EQ(press(&e, &h, (TextKeys){ .enter = true }), TEXT_INPUT_CHANGED);
    CHECK_SV(string_sv(&e.text), SV("\n6789abcdefghij"));
    CHECK_EQ(press(&e, &h, (TextKeys){ .ctrl = true, .enter = true }), TEXT_INPUT_COMMITTED);
    // Escape wins over typing in the same frame.
    k = typed("x");
    k.escape = true;
    CHECK_EQ(press(&e, &h, k), TEXT_INPUT_CANCELLED);
    text_edit_free(&e);
    string_free(&h.clipboard);
}

void register_text_edit_tests(void) {
    test_register("text_edit/basics", test_text_edit_basics);
    test_register("text_edit/utf8", test_text_edit_utf8);
    test_register("text_edit/paste", test_text_edit_paste);
    test_register("text_edit/keys", test_text_edit_keys);
}
