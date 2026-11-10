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
    // Colours ease between rest / hover / checked states instead of snapping.
    bool filled = style == BUTTON_PRIMARY || checked;
    f32 fill = anim_towards(&ui->anim, id.id ^ 0x5EEDu, filled ? 1.0f : 0.0f, 14.0f, filled ? 1.0f : 0.0f);
    f32 hover = anim_towards(&ui->anim, id.id, over ? 1.0f : 0.0f, 16.0f, 0.0f);
    Color rest = style == BUTTON_NORMAL ? c->surface_alt : color_with_alpha(c->hover, 0.0f);
    Color rest_hover = style == BUTTON_NORMAL ? color_lerp(c->surface_alt, c->text, 0.06f) : c->hover;
    Color plain = color_lerp(rest, rest_hover, hover);
    Color accent = color_lerp(c->accent, c->surface, 0.15f * hover);
    Color bg = color_lerp(plain, accent, fill);
    Color fg = color_lerp(c->text, c->accent_text, fill);
    Color border = style == BUTTON_NORMAL ? color_with_alpha(c->border, (1.0f - fill) * c->border.a / 255.0f) : (Color){ 0, 0, 0, 0 };
    if (pressed) bg = color_lerp(bg, c->text, 0.12f);
    if (!enabled) {
        fg = color_with_alpha(c->text_muted, 0.55f);
        if (style == BUTTON_PRIMARY || checked) bg = c->surface_alt;
    }

    // "@icon text" labels: a vector icon, then optional text.
    i32 icon = -1;
    StringView text = label;
    if (sv_starts_with(label, SV("@"))) {
        StringView tail = label;
        icon = icon_find(sv_chop_char(&tail, ' '));
        text = sv_trim(tail);
    }
    bool icon_only = icon >= 0 && text.len == 0;
    u16 pad_x = ui_u16(ui, icon_only ? 6 : 10);
    CLAY({ .id = id,
           .layout = { .padding = { pad_x, pad_x, ui_u16(ui, 6), ui_u16(ui, 6) }, .childGap = ui_u16(ui, 5),
                       .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(bg),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius * 0.6f)),
           .border = { .color = color_to_clay(border), .width = CLAY_BORDER_OUTSIDE(border.a ? 1 : 0) } }) {
        if (icon >= 0) ui_icon(ui, icon, 18, fg);
        if (text.len) {
            CLAY_TEXT(ui_str(ui, text), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 14),
                                                           .textColor = color_to_clay(fg), .wrapMode = CLAY_TEXT_WRAP_NONE }));
        }
    }
    return clicked(ui, id, enabled);
}

void ui_command_button(Ui *ui, StringView command_id, StringView icon, ButtonStyle style, u32 index) {
    ui_command_button_ex(ui, command_id, icon, false, style, index);
}

void ui_command_button_ex(Ui *ui, StringView command_id, StringView icon, bool with_label, ButtonStyle style, u32 index) {
    App *app = ui->app;
    const CommandDef *cmd = command_find(&app->commands, command_id);
    if (!cmd) {
        ui_label(ui, command_id, FONT_UI, 12, ui->theme->colors.danger);
        return;
    }
    char key[128];
    snprintf(key, sizeof(key), "command.%s", cmd->id);
    StringView label = icon.len ? icon : app_tr(app, key, cmd->label);
    if (icon.len && with_label) {
        StringView translated = app_tr(app, key, cmd->label);
        label = arena_format(&ui->frame, SV_FMT " " SV_FMT, SV_ARG(icon), SV_ARG(translated));
    }
    Clay_ElementId id = Clay_GetElementIdWithIndex(ui_str(ui, command_id), index);
    if (ui_button(ui, id, label, style, command_is_checked(&app->commands, cmd), command_is_enabled(&app->commands, cmd))) {
        command_execute(&app->commands, command_id);
    }
}

Clay_ElementDeclaration ui_card(Ui *ui, Clay_ElementId id) {
    const Theme *t = ui->theme;
    return (Clay_ElementDeclaration){
        .id = id,
        .layout = { .sizing = { CLAY_SIZING_GROW(0), ui->in_tab ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0) },
                    .padding = CLAY_PADDING_ALL(ui_u16(ui, t->shape.padding)),
                    .childGap = ui_u16(ui, 8),
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = color_to_clay(t->colors.surface),
        .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, t->shape.radius)),
        .border = { .color = color_to_clay(t->colors.border), .width = CLAY_BORDER_OUTSIDE((u16)t->shape.border_width) },
    };
}

Clay_ElementDeclaration ui_scroll_area(Ui *ui, Clay_ElementId id, f32 height) {
    Clay_SizingAxis h = ui->in_tab ? CLAY_SIZING_GROW(0) : height > 0 ? CLAY_SIZING_FIXED(ui_px(ui, height)) : CLAY_SIZING_FIT(0);
    return (Clay_ElementDeclaration){
        .id = id,
        .layout = { .sizing = { CLAY_SIZING_GROW(0), h }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
    };
}

void ui_card_title(Ui *ui, StringView title) {
    if (ui->in_tab) return;   // the tab bar already names the panel
    CLAY_TEXT(ui_str(ui, title), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 12),
                                                    .textColor = color_to_clay(ui->theme->colors.text_muted),
                                                    .letterSpacing = 1, .wrapMode = CLAY_TEXT_WRAP_NONE }));
}

// ---- Help popovers ----

void ui_help_button(Ui *ui, StringView topic) {
    const ThemeColors *c = &ui->theme->colors;
    bool open = sv_eq(sv(ui->help_topic), topic);
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("HelpButton"), (u32)(topic.len ? (u8)topic.data[0] * 131u + (u32)topic.len * 7u + (u8)topic.data[topic.len - 1] : 0));
    bool over = Clay_PointerOver(id);
    if (over) ui->help_button_hovered = true;
    f32 size = ui_px(ui, 18);
    CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_FIXED(size), CLAY_SIZING_FIXED(size) } } }) {
        ui_icon(ui, icon_find(SV("help")), 18, open || over ? c->accent : c->text_muted);
        if (open) {
            // Popover below the button, right-aligned with it.
            f32 width = ui_px(ui, 300);
            char key[64];
            snprintf(key, sizeof(key), "help." SV_FMT ".title", SV_ARG(topic));
            StringView title = app_tr(ui->app, key, "");
            snprintf(key, sizeof(key), "help." SV_FMT ".text", SV_ARG(topic));
            StringView text = app_tr(ui->app, key, "");
            // Opens upwards when the button is in the lower half of the window.
            Clay_ElementData button = Clay_GetElementData(id);
            bool up = button.found && button.boundingBox.y > (f32)GetScreenHeight() * 0.5f;
            Clay_FloatingAttachPoints attach = up ? (Clay_FloatingAttachPoints){ .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_TOP }
                                                  : (Clay_FloatingAttachPoints){ .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM };
            CLAY({ .id = CLAY_ID("HelpPopover"),
                   .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 150, .offset = { 0, up ? -ui_px(ui, 6) : ui_px(ui, 6) },
                                 .attachPoints = attach },
                   .layout = { .sizing = { CLAY_SIZING_FIXED(width), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                               .padding = CLAY_PADDING_ALL(ui_u16(ui, 12)), .childGap = ui_u16(ui, 6) },
                   .backgroundColor = color_to_clay(c->surface),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)),
                   .border = { .color = color_to_clay(c->accent), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                if (title.len) ui_label(ui, title, FONT_UI_BOLD, 13, c->text);
                // Paragraphs separated by blank lines ("\n\n" in the l10n file).
                StringView rest = text;
                while (rest.len > 0) {
                    isize gap = sv_find(rest, SV("\n\n"));
                    StringView para = gap >= 0 ? sv_take(rest, gap) : rest;
                    rest = gap >= 0 ? sv_drop(rest, gap + 2) : SV("");
                    StringView wrapped = ui_wrap_text(ui, para, FONT_UI, 12, width - ui_px(ui, 24));
                    CLAY_TEXT(ui_str(ui, wrapped), CLAY_TEXT_CONFIG({ .fontId = FONT_UI, .fontSize = ui_u16(ui, 12),
                                                                      .textColor = color_to_clay(c->text_muted), .lineHeight = ui_u16(ui, 17) }));
                }
            }
        }
    }
    if (over && ui->mouse_pressed) {
        if (open) {
            ui->help_topic[0] = '\0';
        } else {
            snprintf(ui->help_topic, sizeof(ui->help_topic), SV_FMT, SV_ARG(topic));
        }
    }
}

void ui_card_title_help(Ui *ui, StringView title, StringView topic) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) { ui_card_title(ui, title); }
        ui_help_button(ui, topic);
    }
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
    f32 t = anim_towards(&ui->anim, id.id, on ? 1.0f : 0.0f, 16.0f, on ? 1.0f : 0.0f);   // knob slides
    f32 hover = anim_towards(&ui->anim, id.id ^ 0xB0B0u, over ? 1.0f : 0.0f, 16.0f, 0.0f);
    Color track = color_lerp(color_lerp(c->surface_alt, c->text, 0.18f), c->accent, t);
    track = color_lerp(track, c->text, 0.08f * hover);
    f32 pad = ui_px(ui, 3);
    u16 left = (u16)(pad + t * (w - knob - 2 * pad) + 0.5f);
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) },
                       .padding = { left, 0, (u16)pad, (u16)pad },
                       .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(track),
           .cornerRadius = CLAY_CORNER_RADIUS(h * 0.5f) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(knob), CLAY_SIZING_FIXED(knob) } },
               .backgroundColor = color_to_clay(color_lerp(c->surface, c->accent_text, t)),
               .cornerRadius = CLAY_CORNER_RADIUS(knob * 0.5f) }) {}
    }
    return over && ui->mouse_pressed;
}
