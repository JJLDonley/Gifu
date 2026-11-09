#include "ui/ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

Clay_String ui_str(Ui *ui, StringView s) {
    StringView copy = arena_copy_sv(&ui->frame, s);
    return (Clay_String){ .isStaticallyAllocated = false, .length = (i32)copy.len, .chars = copy.data };
}

Clay_String ui_fmt(Ui *ui, const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return ui_str(ui, sv_from_parts(buffer, CLAMP(n, 0, (int)sizeof(buffer) - 1)));
}

CustomElement *ui_custom(Ui *ui, CustomDrawFn draw, void *user) {
    CustomElement *e = arena_new(&ui->frame, CustomElement);
    e->draw = draw;
    e->user = user;
    return e;
}

Clay_TextElementConfig *ui_text_style(Ui *ui, FontRole role, f32 size, Color color) {
    return CLAY_TEXT_CONFIG({ .fontId = (u16)role, .fontSize = ui_u16(ui, size), .textColor = color_to_clay(color),
                              .wrapMode = CLAY_TEXT_WRAP_WORDS });
}

void ui_label(Ui *ui, StringView text, FontRole role, f32 size, Color color) {
    CLAY_TEXT(ui_str(ui, text), ui_text_style(ui, role, size, color));
}

static bool clicked(Ui *ui, Clay_ElementId id, bool enabled) {
    bool over = Clay_PointerOver(id);
    if (over && ui->mouse_pressed && enabled) ui->active_id = id.id;
    bool click = over && ui->mouse_released && ui->active_id == id.id && enabled;
    if (ui->mouse_released && ui->active_id == id.id) ui->active_id = 0;
    return click;
}

bool ui_button(Ui *ui, Clay_ElementId id, StringView label, ButtonStyle style, bool checked, bool enabled) {
    const ThemeColors *c = &ui->theme->colors;
    bool over = Clay_PointerOver(id) && enabled;
    bool pressed = over && ui->mouse_down && ui->active_id == id.id;
    Color bg = (Color){ 0, 0, 0, 0 }, fg = c->text, border = (Color){ 0, 0, 0, 0 };
    if (style == BUTTON_PRIMARY || checked) {
        bg = c->accent;
        fg = c->accent_text;
    } else if (style == BUTTON_NORMAL) {
        bg = c->surface_alt;
        border = c->border;
    }
    if (over && !(style == BUTTON_PRIMARY || checked)) {
        bg = style == BUTTON_NORMAL ? color_lerp(c->surface_alt, c->text, 0.06f) : c->hover;
    }
    if (over && (style == BUTTON_PRIMARY || checked)) bg = color_lerp(c->accent, c->surface, 0.15f);
    if (pressed) bg = color_lerp(bg, c->text, 0.12f);
    if (!enabled) {
        fg = color_with_alpha(c->text_muted, 0.55f);
        if (style == BUTTON_PRIMARY || checked) bg = c->surface_alt;
    }

    CLAY({ .id = id,
           .layout = { .padding = { ui_u16(ui, 10), ui_u16(ui, 10), ui_u16(ui, 6), ui_u16(ui, 6) },
                       .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(bg),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius * 0.6f)),
           .border = { .color = color_to_clay(border), .width = CLAY_BORDER_OUTSIDE(border.a ? 1 : 0) } }) {
        CLAY_TEXT(ui_str(ui, label), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 14),
                                                        .textColor = color_to_clay(fg), .wrapMode = CLAY_TEXT_WRAP_NONE }));
    }
    return clicked(ui, id, enabled);
}

void ui_command_button(Ui *ui, StringView command_id, StringView icon, ButtonStyle style, u32 index) {
    App *app = ui->app;
    const CommandDef *cmd = command_find(&app->commands, command_id);
    if (!cmd) {
        ui_label(ui, command_id, FONT_UI, 12, ui->theme->colors.danger);
        return;
    }
    char key[128];
    snprintf(key, sizeof(key), "command.%s", cmd->id);
    StringView label = icon.len ? icon : app_tr(app, key, cmd->label);
    Clay_ElementId id = Clay_GetElementIdWithIndex(ui_str(ui, command_id), index);
    if (ui_button(ui, id, label, style, command_is_checked(&app->commands, cmd), command_is_enabled(&app->commands, cmd))) {
        command_execute(&app->commands, command_id);
    }
}

Clay_ElementDeclaration ui_card(Ui *ui, Clay_ElementId id) {
    const Theme *t = ui->theme;
    return (Clay_ElementDeclaration){
        .id = id,
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(ui_u16(ui, t->shape.padding)),
                    .childGap = ui_u16(ui, 8),
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = color_to_clay(t->colors.surface),
        .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, t->shape.radius)),
        .border = { .color = color_to_clay(t->colors.border), .width = CLAY_BORDER_OUTSIDE((u16)t->shape.border_width) },
    };
}

void ui_card_title(Ui *ui, StringView title) {
    CLAY_TEXT(ui_str(ui, title), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 12),
                                                    .textColor = color_to_clay(ui->theme->colors.text_muted),
                                                    .letterSpacing = 1, .wrapMode = CLAY_TEXT_WRAP_NONE }));
}

static bool is_cjk(u32 cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2FFFF);
}

StringView ui_wrap_text(Ui *ui, StringView text, FontRole role, f32 size, f32 max_width) {
    f32 px = ui_px(ui, size);
    String out = { 0 };
    string_reserve(&out, text.len + 32);
    f32 line_width = 0;
    isize last_break_out = -1;      // index in `out` where a break may be inserted
    f32 width_at_break = 0;
    StringView rest = text;
    while (rest.len > 0) {
        StringView before = rest;
        u32 cp = utf8_next(&rest);
        StringView ch = sv_take(before, before.len - rest.len);
        if (cp == '\n') {
            string_push_char(&out, '\n');
            line_width = 0;
            last_break_out = -1;
            continue;
        }
        f32 w = font_measure(ui->fonts, role, px, ch).x;
        if (line_width + w > max_width && line_width > 0) {
            if (cp == ' ') {   // break at this space, drop it
                string_push_char(&out, '\n');
                line_width = 0;
                last_break_out = -1;
                continue;
            }
            if (is_cjk(cp) || last_break_out < 0) {
                string_push_char(&out, '\n');
                line_width = 0;
            } else {
                out.data[last_break_out] = '\n';   // turn the last space into a newline
                line_width -= width_at_break;
            }
            last_break_out = -1;
        }
        if (cp == ' ') {
            last_break_out = out.len;
            width_at_break = line_width + w;
        } else if (is_cjk(cp)) {
            last_break_out = -1;
        }
        string_push(&out, ch);
        line_width += w;
    }
    StringView result = arena_copy_sv(&ui->frame, string_sv(&out));
    string_free(&out);
    return result;
}

bool ui_toggle(Ui *ui, Clay_ElementId id, bool on) {
    const ThemeColors *c = &ui->theme->colors;
    f32 w = ui_px(ui, 38), h = ui_px(ui, 22), knob = h - ui_px(ui, 6);
    bool over = Clay_PointerOver(id);
    Color track = on ? c->accent : color_lerp(c->surface_alt, c->text, 0.18f);
    if (over) track = color_lerp(track, c->text, 0.08f);
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) },
                       .padding = CLAY_PADDING_ALL(ui_u16(ui, 3)),
                       .childAlignment = { on ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(track),
           .cornerRadius = CLAY_CORNER_RADIUS(h * 0.5f) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(knob), CLAY_SIZING_FIXED(knob) } },
               .backgroundColor = color_to_clay(on ? c->accent_text : c->surface),
               .cornerRadius = CLAY_CORNER_RADIUS(knob * 0.5f) }) {}
    }
    return over && ui->mouse_pressed;
}
