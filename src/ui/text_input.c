// Immediate-mode text input (single or multi-line).
//
// The caller owns a TextEdit (text + caret/selection). Each frame
// ui_text_input() handles focus, mouse and keyboard, lays the text out into
// visual lines (wrapping at spaces, or anywhere inside CJK runs) and emits a
// Clay custom element that draws text, selection and caret.
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { MAX_VISUAL_LINES = 2048 };

typedef struct VisualLine {
    isize start, end;   // byte range in edit->text (end excludes the newline)
} VisualLine;

typedef struct TextLayout {
    VisualLine *lines;
    i32 count;
    f32 line_height;
    f32 font_px;
} TextLayout;

typedef struct DrawState {
    Ui *ui;
    TextEdit *edit;
    TextLayout layout;
    bool focused;
    StringView placeholder;
    f32 padding;
} DrawState;

// ---- UTF-8 helpers ------------------------------------------------------------------------

static bool is_cjk_cp(u32 cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2FFFF);
}

// ---- Layout ----------------------------------------------------------------------------------

static TextLayout layout_text(Ui *ui, const TextEdit *edit, f32 font_px, f32 max_width) {
    TextLayout out = { .font_px = font_px };
    out.lines = arena_new_array(&ui->frame, VisualLine, MAX_VISUAL_LINES);
    out.line_height = font_line_height(ui->fonts, FONT_UI, font_px);
    StringView text = string_sv(&edit->text);
    isize line_start = 0, last_space = -1;
    f32 width = 0, width_after_space = 0;
    isize i = 0;
    while (i <= text.len && out.count < MAX_VISUAL_LINES) {
        if (i == text.len) {
            out.lines[out.count++] = (VisualLine){ line_start, i };
            break;
        }
        if (text.data[i] == '\n') {
            out.lines[out.count++] = (VisualLine){ line_start, i };
            line_start = i + 1;
            i = line_start;
            width = 0;
            last_space = -1;
            continue;
        }
        if (!edit->multiline) {   // single line: never wrap
            i = utf8_next_boundary(text, i);
            continue;
        }
        isize next = utf8_next_boundary(text, i);
        StringView ch = sv_slice(text, i, next);
        StringView probe = ch;
        u32 cp = utf8_next(&probe);
        f32 w = font_measure(ui->fonts, FONT_UI, font_px, ch).x;
        if (width + w > max_width && i > line_start) {
            if (cp != ' ' && !is_cjk_cp(cp) && last_space > line_start) {
                out.lines[out.count++] = (VisualLine){ line_start, last_space };
                line_start = last_space + 1;
                width -= width_after_space;
            } else {
                out.lines[out.count++] = (VisualLine){ line_start, i };
                line_start = i;
                width = 0;
            }
            last_space = -1;
        }
        if (cp == ' ') {
            last_space = i;
            width_after_space = width + w;
        }
        width += w;
        i = next;
    }
    if (out.count == 0) out.lines[out.count++] = (VisualLine){ 0, 0 };
    return out;
}

static i32 line_of(const TextLayout *layout, isize offset) {
    for (i32 i = 0; i < layout->count; ++i) {
        bool last = i == layout->count - 1;
        if (offset >= layout->lines[i].start && (offset <= layout->lines[i].end) && (last || offset < layout->lines[i + 1].start)) return i;
    }
    return layout->count - 1;
}

static f32 x_of(Ui *ui, const TextEdit *edit, const TextLayout *layout, i32 line, isize offset) {
    StringView text = string_sv(&edit->text);
    isize start = layout->lines[line].start;
    return font_measure(ui->fonts, FONT_UI, layout->font_px, sv_slice(text, start, MIN(offset, layout->lines[line].end))).x;
}

static isize offset_at_x(Ui *ui, const TextEdit *edit, const TextLayout *layout, i32 line, f32 x) {
    StringView text = string_sv(&edit->text);
    VisualLine l = layout->lines[line];
    f32 pen = 0;
    for (isize i = l.start; i < l.end;) {
        isize next = utf8_next_boundary(text, i);
        f32 w = font_measure(ui->fonts, FONT_UI, layout->font_px, sv_slice(text, i, next)).x;
        if (x < pen + w * 0.5f) return i;
        pen += w;
        i = next;
    }
    return l.end;
}

// ---- Editing -----------------------------------------------------------------------------------

static bool key(i32 k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }

typedef struct KeyHost {
    Ui *ui;
    const TextLayout *layout;
} KeyHost;

static isize host_vertical(void *user, const TextEdit *e, i32 direction) {
    const KeyHost *h = user;
    i32 line = line_of(h->layout, e->caret), target = line + direction;
    if (target < 0) return 0;
    if (target >= h->layout->count) return e->text.len;
    return offset_at_x(h->ui, e, h->layout, target, x_of(h->ui, e, h->layout, line, e->caret));
}

static void host_line_bounds(void *user, const TextEdit *e, isize caret, isize *start, isize *end) {
    UNUSED(e);
    const KeyHost *h = user;
    VisualLine l = h->layout->lines[line_of(h->layout, caret)];
    *start = l.start, *end = l.end;
}

static void host_set_clipboard(void *user, StringView text) {
    UNUSED(user);
    String copy = string_from(text);
    SetClipboardText(string_cstr(&copy));
    string_free(&copy);
}

static StringView host_get_clipboard(void *user) {
    UNUSED(user);
    const char *clip = GetClipboardText();
    return sv(clip ? clip : "");
}

// Reads this frame's keys from raylib; the editing itself is text_edit_apply_keys (unit-tested).
static TextInputResult handle_keys(Ui *ui, TextEdit *e, const TextLayout *layout) {
    TextKeys k = { 0 };
    k.ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    k.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    for (i32 cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
        if (k.char_count < (i32)countof(k.chars)) k.chars[k.char_count++] = (u32)cp;
    }
    k.backspace = key(KEY_BACKSPACE), k.del = key(KEY_DELETE);
    k.left = key(KEY_LEFT), k.right = key(KEY_RIGHT), k.up = key(KEY_UP), k.down = key(KEY_DOWN);
    k.home = key(KEY_HOME), k.end = key(KEY_END);
    k.enter = key(KEY_ENTER) || key(KEY_KP_ENTER), k.escape = IsKeyPressed(KEY_ESCAPE);
    k.select_all = k.ctrl && IsKeyPressed(KEY_A);
    k.copy = k.ctrl && IsKeyPressed(KEY_C), k.cut = k.ctrl && IsKeyPressed(KEY_X), k.paste = k.ctrl && IsKeyPressed(KEY_V);
    KeyHost h = { ui, layout };
    TextEditHost host = { &h, host_vertical, host_line_bounds, host_set_clipboard, host_get_clipboard };
    return text_edit_apply_keys(e, &k, &host);
}

// ---- Drawing ------------------------------------------------------------------------------------

static void draw_text_input(void *user, Rectangle r) {
    DrawState *st = user;
    Ui *ui = st->ui;
    const ThemeColors *c = &ui->theme->colors;
    TextEdit *e = st->edit;
    const TextLayout *layout = &st->layout;
    f32 radius = ui_px(ui, 6);
    DrawRectangleRounded(r, radius * 2 / fminf(r.width, r.height), 8, color_with_alpha(c->surface_alt, 0.9f));
    DrawRectangleRoundedLinesEx(r, radius * 2 / fminf(r.width, r.height), 8, st->focused ? 2.0f : 1.0f,
                                st->focused ? c->accent : c->border);
    render_push_scissor(r);
    f32 x0 = r.x + st->padding, y0 = r.y + st->padding;
    StringView text = string_sv(&e->text);
    if (text.len == 0 && !st->focused && st->placeholder.len) {
        font_draw(ui->fonts, FONT_UI, layout->font_px, st->placeholder, (Vector2){ x0, y0 }, c->text_muted);
    }
    isize sel_a = MIN(e->caret, e->anchor), sel_b = MAX(e->caret, e->anchor);
    for (i32 i = 0; i < layout->count; ++i) {
        VisualLine l = layout->lines[i];
        f32 y = y0 + layout->line_height * (f32)i;
        if (st->focused && sel_a != sel_b && sel_b > l.start && sel_a <= l.end) {
            f32 xa = x_of(ui, e, layout, i, MAX(sel_a, l.start));
            f32 xb = x_of(ui, e, layout, i, MIN(sel_b, l.end));
            if (sel_b > l.end) xb += layout->font_px * 0.3f;   // selected newline
            DrawRectangleRec((Rectangle){ x0 + xa, y, xb - xa, layout->line_height }, color_with_alpha(c->accent, 0.25f));
        }
        font_draw(ui->fonts, FONT_UI, layout->font_px, sv_slice(text, l.start, l.end), (Vector2){ x0, y }, c->text);
    }
    if (st->focused && fmod(GetTime() - e->blink_start, 1.0) < 0.6) {
        i32 line = line_of(layout, e->caret);
        f32 x = x0 + x_of(ui, e, layout, line, e->caret);
        f32 y = y0 + layout->line_height * (f32)line;
        DrawRectangleRec((Rectangle){ floorf(x), y + 2, fmaxf(1.0f, ui->scale * 1.5f), layout->line_height - 4 }, c->text);
    }
    render_pop_scissor();
}

// ---- Widget -----------------------------------------------------------------------------------------

TextInputResult ui_text_input(Ui *ui, Clay_ElementId id, TextEdit *edit, f32 font_size, i32 min_rows, StringView placeholder) {
    TextInputResult result = TEXT_INPUT_NONE;
    f32 font_px = ui_px(ui, font_size);
    f32 padding = ui_px(ui, 8);
    Clay_ElementData prev = Clay_GetElementData(id);
    f32 inner_width = prev.found ? prev.boundingBox.width - 2 * padding : ui_px(ui, 300);
    bool focused = ui->focus_id == id.id;
    bool over = Clay_PointerOver(id);

    TextLayout layout = layout_text(ui, edit, font_px, fmaxf(inner_width, 40.0f));

    // Mouse: focus, caret placement, drag selection.
    if (ui->mouse_pressed) {
        if (over && !ui->pointer_over_menu) {
            if (!focused) {
                ui->focus_id = id.id;
                focused = true;
                edit->anchor = edit->caret;
            }
            ui->text_dragging = true;
        } else if (focused) {
            ui->focus_id = 0;
            focused = false;
            result = TEXT_INPUT_COMMITTED;
        }
    }
    if (!ui->mouse_down) ui->text_dragging = false;
    if (focused && prev.found && (ui->mouse_pressed || ui->text_dragging) && (over || ui->text_dragging)) {
        f32 local_y = ui->mouse.y - prev.boundingBox.y - padding;
        i32 line = CLAMP((i32)floorf(local_y / layout.line_height), 0, layout.count - 1);
        isize hit = offset_at_x(ui, edit, &layout, line, ui->mouse.x - prev.boundingBox.x - padding);
        edit->caret = hit;
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (ui->mouse_pressed && !shift) edit->anchor = hit;
        edit->blink_start = GetTime();
    }

    if (focused && result == TEXT_INPUT_NONE) {
        result = handle_keys(ui, edit, &layout);
        if (result != TEXT_INPUT_NONE) edit->blink_start = GetTime();
        if (result == TEXT_INPUT_COMMITTED || result == TEXT_INPUT_CANCELLED) {
            ui->focus_id = 0;
            focused = false;
        }
        if (result == TEXT_INPUT_CHANGED) layout = layout_text(ui, edit, font_px, fmaxf(inner_width, 40.0f));
    }
    if (focused) ui->focus_claimed = true;

    i32 rows = MAX(min_rows, layout.count);
    DrawState *st = arena_new(&ui->frame, DrawState);
    *st = (DrawState){ ui, edit, layout, focused, arena_copy_sv(&ui->frame, placeholder), padding };
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(layout.line_height * (f32)rows + 2 * padding) } },
           .custom = { .customData = ui_custom(ui, draw_text_input, st) } }) {}
    if (over) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    return result;
}

// ---- Number input ---------------------------------------------------------------------------------

static TextEdit *number_slot(Ui *ui, u32 id, bool *fresh) {
    i32 free_slot = -1, oldest = 0;
    for (i32 i = 0; i < (i32)countof(ui->number_fields); ++i) {
        if (ui->number_fields[i].id == id) {
            ui->number_fields[i].frame = ui->number_frame;
            *fresh = false;
            return &ui->number_fields[i].edit;
        }
        if (!ui->number_fields[i].id && free_slot < 0) free_slot = i;
        if (ui->number_fields[i].frame < ui->number_fields[oldest].frame) oldest = i;
    }
    i32 i = free_slot >= 0 ? free_slot : oldest;   // reuse the field not drawn for the longest
    ui->number_fields[i].id = id;
    ui->number_fields[i].frame = ui->number_frame;
    *fresh = true;
    return &ui->number_fields[i].edit;
}

// Keeps only what a number may contain.
static void number_filter(TextEdit *edit, bool negative) {
    String out = { 0 };
    bool dot = false;
    StringView t = string_sv(&edit->text);
    for (isize i = 0; i < t.len; ++i) {
        char ch = t.data[i];
        if ((ch >= '0' && ch <= '9') || (ch == '.' && !dot) || (ch == '-' && negative && out.len == 0)) {
            string_push_char(&out, ch);
            if (ch == '.') dot = true;
        }
    }
    if (out.len != t.len) {
        text_edit_set(edit, string_sv(&out));
        edit->caret = edit->anchor = edit->text.len;
    }
    string_free(&out);
}

bool ui_number_input(Ui *ui, Clay_ElementId id, f64 *value, f64 lo, f64 hi, const char *fmt, f32 width) {
    bool fresh = false;
    TextEdit *edit = number_slot(ui, id.id, &fresh);
    bool focused = ui->focus_id == id.id;
    char shown[48];
    snprintf(shown, sizeof(shown), fmt, *value);
    // Not being edited: show the current value (it may change elsewhere).
    if (!focused && (fresh || !sv_eq(string_sv(&edit->text), sv(shown)))) text_edit_set(edit, sv(shown));
    TextInputResult r = TEXT_INPUT_NONE;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, width)), CLAY_SIZING_FIT(0) } } }) {
        r = ui_text_input(ui, id, edit, 14, 1, SV(""));
    }
    if (ui->focus_id == id.id && !focused) text_edit_select_all(edit);   // just clicked: typing replaces it
    number_filter(edit, lo < 0);
    if (r == TEXT_INPUT_CANCELLED) {
        text_edit_set(edit, sv(shown));
        return false;
    }
    if (r != TEXT_INPUT_COMMITTED) return false;
    f64 typed = 0;
    StringView text = sv_trim(string_sv(&edit->text));
    if (!text.len || !sv_parse_f64(text, &typed)) {
        text_edit_set(edit, sv(shown));
        return false;
    }
    typed = CLAMP(typed, lo, hi);
    bool changed = fabs(typed - *value) > 1e-9;
    *value = typed;
    snprintf(shown, sizeof(shown), fmt, *value);
    text_edit_set(edit, sv(shown));
    return changed;
}
