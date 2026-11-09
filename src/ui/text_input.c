// Immediate-mode text input (single or multi-line).
//
// The caller owns a TextEdit (text + caret/selection). Each frame
// ui_text_input() handles focus, mouse and keyboard, lays the text out into
// visual lines (wrapping at spaces, or anywhere inside CJK runs) and emits a
// Clay custom element that draws text, selection and caret.
#include "ui/ui.h"

#include <math.h>
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

static isize next_boundary(StringView s, isize i) {
    if (i >= s.len) return s.len;
    ++i;
    while (i < s.len && ((u8)s.data[i] & 0xC0) == 0x80) ++i;
    return i;
}

static isize prev_boundary(StringView s, isize i) {
    if (i <= 0) return 0;
    --i;
    while (i > 0 && ((u8)s.data[i] & 0xC0) == 0x80) --i;
    return i;
}

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
            i = next_boundary(text, i);
            continue;
        }
        isize next = next_boundary(text, i);
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
        isize next = next_boundary(text, i);
        f32 w = font_measure(ui->fonts, FONT_UI, layout->font_px, sv_slice(text, i, next)).x;
        if (x < pen + w * 0.5f) return i;
        pen += w;
        i = next;
    }
    return l.end;
}

// ---- Editing -----------------------------------------------------------------------------------

static bool has_selection(const TextEdit *e) { return e->caret != e->anchor; }

static void delete_selection(TextEdit *e) {
    isize a = MIN(e->caret, e->anchor), b = MAX(e->caret, e->anchor);
    if (a == b) return;
    memmove(e->text.data + a, e->text.data + b, (usize)(e->text.len - b));
    e->text.len -= b - a;
    e->text.data[e->text.len] = '\0';
    e->caret = e->anchor = a;
}

static void insert_text(TextEdit *e, StringView s) {
    delete_selection(e);
    string_reserve(&e->text, e->text.len + s.len);
    memmove(e->text.data + e->caret + s.len, e->text.data + e->caret, (usize)(e->text.len - e->caret));
    memcpy(e->text.data + e->caret, s.data, (usize)s.len);
    e->text.len += s.len;
    e->text.data[e->text.len] = '\0';
    e->caret += s.len;
    e->anchor = e->caret;
}

void text_edit_set(TextEdit *e, StringView text) {
    string_clear(&e->text);
    string_push(&e->text, text);
    e->caret = e->anchor = e->text.len;
}

void text_edit_free(TextEdit *e) { string_free(&e->text); }

static bool key(i32 k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }

static TextInputResult handle_keys(Ui *ui, TextEdit *e, const TextLayout *layout) {
    TextInputResult result = TEXT_INPUT_NONE;
    StringView text = string_sv(&e->text);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    for (i32 cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
        if (cp < 0x20 || cp == 0x7F) continue;
        char utf8[4];
        insert_text(e, sv_from_parts(utf8, utf8_encode((u32)cp, utf8)));
        result = TEXT_INPUT_CHANGED;
    }
    text = string_sv(&e->text);

    if (key(KEY_BACKSPACE)) {
        if (!has_selection(e)) e->anchor = ctrl ? 0 : prev_boundary(text, e->caret);
        delete_selection(e);
        result = TEXT_INPUT_CHANGED;
    }
    if (key(KEY_DELETE)) {
        if (!has_selection(e)) e->anchor = next_boundary(text, e->caret);
        delete_selection(e);
        result = TEXT_INPUT_CHANGED;
    }
    text = string_sv(&e->text);
    isize caret = e->caret;
    if (key(KEY_LEFT)) caret = has_selection(e) && !shift ? MIN(e->caret, e->anchor) : prev_boundary(text, caret);
    if (key(KEY_RIGHT)) caret = has_selection(e) && !shift ? MAX(e->caret, e->anchor) : next_boundary(text, caret);
    if (key(KEY_UP) || key(KEY_DOWN)) {
        i32 line = line_of(layout, caret);
        i32 target = key(KEY_UP) ? line - 1 : line + 1;
        if (target >= 0 && target < layout->count) caret = offset_at_x(ui, e, layout, target, x_of(ui, e, layout, line, caret));
        else caret = target < 0 ? 0 : text.len;
    }
    if (key(KEY_HOME)) caret = ctrl ? 0 : layout->lines[line_of(layout, caret)].start;
    if (key(KEY_END)) caret = ctrl ? text.len : layout->lines[line_of(layout, caret)].end;
    if (caret != e->caret) {
        e->caret = caret;
        if (!shift) e->anchor = caret;
    }

    if (ctrl && IsKeyPressed(KEY_A)) {
        e->anchor = 0;
        e->caret = e->text.len;
    }
    if (ctrl && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X)) && has_selection(e)) {
        isize a = MIN(e->caret, e->anchor), b = MAX(e->caret, e->anchor);
        String copy = string_from(sv_slice(string_sv(&e->text), a, b));
        SetClipboardText(string_cstr(&copy));
        string_free(&copy);
        if (IsKeyPressed(KEY_X)) {
            delete_selection(e);
            result = TEXT_INPUT_CHANGED;
        }
    }
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip && *clip) {
            String clean = { 0 };
            for (const char *p = clip; *p; ++p) {
                if (*p == '\r') continue;
                if (*p == '\n' && !e->multiline) continue;
                string_push_char(&clean, *p);
            }
            insert_text(e, string_sv(&clean));
            string_free(&clean);
            result = TEXT_INPUT_CHANGED;
        }
    }
    if (key(KEY_ENTER) || key(KEY_KP_ENTER)) {
        if (e->multiline && !ctrl) {
            insert_text(e, SV("\n"));
            result = TEXT_INPUT_CHANGED;
        } else {
            result = TEXT_INPUT_COMMITTED;
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) result = TEXT_INPUT_CANCELLED;
    return result;
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
    BeginScissorMode((i32)r.x, (i32)r.y, (i32)r.width, (i32)r.height);
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
    EndScissorMode();
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
