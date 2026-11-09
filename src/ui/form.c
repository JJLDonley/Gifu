// Data-driven forms and modal dialogs.
//
// assets/data/forms.json describes each form ("settings", "new_game", ...):
// sections of fields bound to config keys (the config field table). Fields
// apply immediately; dialogs add their own footer buttons around the form.
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static StringView label_of(Ui *ui, JsonValue v) {
    const char *key = json_string(json_get(v, "label"), SV("")).data;
    StringView fallback = json_string(json_get(v, "fallback"), json_string(json_get(v, "key"), SV("?")));
    return key && *key ? i18n_get(&ui->app->i18n, sv(key), fallback) : fallback;
}

static StringView title_of(Ui *ui, JsonValue v) {
    StringView key = json_string(json_get(v, "title"), SV(""));
    return i18n_get(&ui->app->i18n, key, json_string(json_get(v, "fallback"), key));
}

static void apply_change(Ui *ui, const ConfigField *field) {
    StringView key = sv(field->key);
    if (sv_eq(key, SV("theme"))) {
        isize index = themes_find(&ui->themes, config_get_string(&ui->app->config, field));
        if (index >= 0) ui_apply_theme(ui, index);
    }
    if (sv_eq(key, SV("ui_scale"))) Clay_ResetMeasureTextCache();
    app_config_changed(ui->app, key);
}

// ---- Field widgets --------------------------------------------------------------------------

static void field_label(Ui *ui, StringView label) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
        ui_label(ui, label, FONT_UI, 14, ui->theme->colors.text);
    }
}

static void bool_field(Ui *ui, JsonValue v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        field_label(ui, label_of(ui, v));
        if (ui_toggle(ui, CLAY_IDI("FormToggle", index), config_get_bool(config, f))) {
            config_set_bool(config, f, !config_get_bool(config, f));
            apply_change(ui, f);
        }
    }
}

static void number_field(Ui *ui, JsonValue v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    f64 value = config_get_number(config, f);
    f64 step = json_number(json_get(v, "step"), 1.0);
    f64 lo = json_number(json_get(v, "min"), f->min), hi = json_number(json_get(v, "max"), f->max);
    char text[48];
    String fmt = string_from(json_string(json_get(v, "format"), SV("%g")));
    snprintf(text, sizeof(text), string_cstr(&fmt), value);
    string_free(&fmt);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        field_label(ui, label_of(ui, v));
        f64 next = value;
        if (ui_button(ui, CLAY_IDI("FormMinus", index), SV("−"), BUTTON_NORMAL, false, value > lo + 1e-9)) next = value - step;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 72)), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
            ui_label(ui, sv(text), FONT_MONO, 14, ui->theme->colors.text);
        }
        if (ui_button(ui, CLAY_IDI("FormPlus", index), SV("+"), BUTTON_NORMAL, false, value < hi - 1e-9)) next = value + step;
        if (next != value) {
            config_set_number(config, f, CLAMP(round(next / step) * step, lo, hi));
            apply_change(ui, f);
        }
    }
}

typedef struct ChoiceOption {
    StringView value;   // string fields
    f64 number;         // numeric fields
    StringView label;
    bool available;
} ChoiceOption;

static isize collect_options(Ui *ui, JsonValue v, ChoiceOption *out, isize cap) {
    StringView source = json_string(json_get(v, "source"), SV(""));
    isize n = 0;
    if (sv_eq(source, SV("themes"))) {
        for (isize i = 0; i < ui->themes.len && n < cap; ++i) {
            out[n++] = (ChoiceOption){ string_sv(&ui->themes.items[i].id), 0, string_sv(&ui->themes.items[i].name), true };
        }
    } else if (sv_eq(source, SV("engines"))) {
        vec_foreach(EngineProfile, p, &ui->app->engine_profiles) {
            if (n < cap) out[n++] = (ChoiceOption){ string_sv(&p->id), 0, string_sv(&p->name), engine_profile_available(p) };
        }
    } else {
        JsonValue options = json_get(v, "options");
        for (isize i = 0; i < json_array_len(options) && n < cap; ++i) {
            JsonValue o = json_array_at(options, i);
            JsonValue value = json_get(o, "value");
            StringView text = json_string(value, SV(""));
            out[n++] = (ChoiceOption){ text, json_number(value, 0), json_string(json_get(o, "label"), text), true };
        }
    }
    return n;
}

static void choice_field(Ui *ui, JsonValue v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    ChoiceOption options[32];
    isize count = collect_options(ui, v, options, countof(options));
    i32 columns = (i32)CLAMP(json_int(json_get(v, "columns"), 3), 1, 6);
    bool numeric = f->type != CONFIG_STRING;
    StringView current = config_get_string(config, f);
    f64 current_number = config_get_number(config, f);
    // An empty engine id means "first available": highlight what is running.
    if (sv_eq(sv(f->key), SV("engine")) && current.len == 0 && ui->app->engine_profile) current = string_sv(&ui->app->engine_profile->id);

    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 6) } }) {
        ui_label(ui, label_of(ui, v), FONT_UI, 14, ui->theme->colors.text);
        for (isize row = 0; row * columns < count; ++row) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6) } }) {
                for (isize col = 0; col < columns; ++col) {
                    isize i = row * columns + col;
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(1.0f / (f32)columns - 0.02f), CLAY_SIZING_FIT(0) } } }) {
                        if (i >= count) continue;
                        bool selected = numeric ? fabs(options[i].number - current_number) < 1e-9 : sv_eq(options[i].value, current);
                        Clay_ElementId id = CLAY_IDI("FormChoice", index * 64 + (u32)i);
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                            if (ui_button(ui, id, options[i].label, BUTTON_NORMAL, selected, options[i].available) && !selected) {
                                if (numeric) config_set_number(config, f, options[i].number);
                                else config_set_string(config, f, options[i].value);
                                apply_change(ui, f);
                            }
                        }
                    }
                }
            }
        }
    }
}

static TextEdit *edit_for(Ui *ui, const ConfigField *f) {
    for (i32 i = 0; i < ui->form_edit_count; ++i) {
        if (ui->form_edit_fields[i] == f) return &ui->form_edits[i];
    }
    if (ui->form_edit_count >= (i32)countof(ui->form_edits)) return NULL;
    i32 i = ui->form_edit_count++;
    ui->form_edit_fields[i] = f;
    text_edit_set(&ui->form_edits[i], config_get_string(&ui->app->config, f));
    return &ui->form_edits[i];
}

static void text_field(Ui *ui, JsonValue v, const ConfigField *f, u32 index) {
    TextEdit *edit = edit_for(ui, f);
    if (!edit) return;
    Clay_ElementId id = CLAY_IDI("FormText", index);
    // Pick up external changes when not being edited.
    if (ui->focus_id != id.id && !sv_eq(string_sv(&edit->text), config_get_string(&ui->app->config, f))) {
        text_edit_set(edit, config_get_string(&ui->app->config, f));
    }
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 120)), CLAY_SIZING_FIT(0) } } }) {
            ui_label(ui, label_of(ui, v), FONT_UI, 14, ui->theme->colors.text);
        }
        TextInputResult r = ui_text_input(ui, id, edit, 14, 1, json_string(json_get(v, "placeholder"), SV("")));
        if (r == TEXT_INPUT_CHANGED || r == TEXT_INPUT_COMMITTED) {
            config_set_string(&ui->app->config, f, string_sv(&edit->text));
            apply_change(ui, f);
        }
    }
}

// ---- Forms ----------------------------------------------------------------------------------------

JsonValue ui_form(Ui *ui, const char *name) { return json_get(json_root(&ui->forms_doc), name); }

void form_render(Ui *ui, JsonValue form) {
    const ThemeColors *c = &ui->theme->colors;
    JsonValue sections = json_get(form, "sections");
    u32 index = 0;
    for (isize s = 0; s < json_array_len(sections); ++s) {
        JsonValue section = json_array_at(sections, s);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                           .childGap = ui_u16(ui, 10) } }) {
            ui_card_title(ui, title_of(ui, section));
            JsonValue fields = json_get(section, "fields");
            for (isize i = 0; i < json_array_len(fields); ++i, ++index) {
                JsonValue v = json_array_at(fields, i);
                const ConfigField *f = config_field(json_string(json_get(v, "key"), SV("")));
                StringView type = json_string(json_get(v, "type"), SV(""));
                if (!f) {
                    ui_label(ui, json_string(json_get(v, "key"), SV("?")), FONT_UI, 12, c->danger);
                    continue;
                }
                if (sv_eq(type, SV("bool"))) bool_field(ui, v, f, index);
                else if (sv_eq(type, SV("number"))) number_field(ui, v, f, index);
                else if (sv_eq(type, SV("choice"))) choice_field(ui, v, f, index);
                else if (sv_eq(type, SV("text"))) text_field(ui, v, f, index);
            }
        }
    }
}

// ---- Modal shell ------------------------------------------------------------------------------------

void ui_modal(Ui *ui, JsonValue form, f32 width, ModalSection footer, void *user) {
    const ThemeColors *c = &ui->theme->colors;
    const Theme *t = ui->theme;
    f32 sw = (f32)GetScreenWidth(), sh = (f32)GetScreenHeight();
    Clay_ElementId dialog = CLAY_ID("ModalDialog");
    f32 open = anim_towards(&ui->anim, dialog.id, 1.0f, 14.0f, 0.0f);   // reset to 0 when a dialog opens
    f32 slide = (1.0f - ease_out_cubic(open)) * ui_px(ui, 48);
    CLAY({ .id = CLAY_ID("ModalBackdrop"),
           .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 200 },
           .layout = { .sizing = { CLAY_SIZING_FIXED(sw), CLAY_SIZING_FIXED(sh) },
                       .padding = { 0, 0, (u16)slide, 0 },
                       .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(color_with_alpha(BLACK, (t->dark ? 0.55f : 0.30f) * open)) }) {
        CLAY({ .id = dialog,
               .layout = { .sizing = { CLAY_SIZING_FIXED(fminf(ui_px(ui, width), sw - 40)), CLAY_SIZING_FIT(0, sh * 0.88f) },
                           .layoutDirection = CLAY_TOP_TO_BOTTOM },
               .backgroundColor = color_to_clay(color_with_alpha(c->surface, 1.0f)),
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, t->shape.radius * 1.5f)),
               .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
            // Header.
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                               .padding = { ui_u16(ui, 20), ui_u16(ui, 14), ui_u16(ui, 14), ui_u16(ui, 10) },
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                    ui_label(ui, title_of(ui, form), FONT_UI_BOLD, 18, c->text);
                }
                if (ui_button(ui, CLAY_ID("ModalClose"), SV("@close"), BUTTON_GHOST, false, true)) ui_close_dialog(ui);
            }
            // Body (scrolls).
            CLAY({ .id = CLAY_ID("ModalBody"),
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                               .padding = { ui_u16(ui, 20), ui_u16(ui, 20), ui_u16(ui, 12), ui_u16(ui, 20) },
                               .childGap = ui_u16(ui, 14) },
                   .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
                form_render(ui, form);
            }
            if (footer) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                                   .padding = { ui_u16(ui, 20), ui_u16(ui, 20), ui_u16(ui, 12), ui_u16(ui, 14) },
                                   .childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } },
                       .border = { .color = color_to_clay(c->border), .width = { .top = 1 } } }) {
                    footer(ui, user);
                }
            }
        }
    }
    ui->pointer_over_menu = true;   // nothing underneath reacts while a modal is open
    if (ui->mouse_pressed && !Clay_PointerOver(dialog)) ui_close_dialog(ui);
}

void ui_close_dialog(Ui *ui) {
    ui->dialog = UI_DIALOG_NONE;
    ui->focus_id = 0;
}

// ---- Dialogs --------------------------------------------------------------------------------------------

static void new_game_footer(Ui *ui, void *user) {
    UNUSED(user);
    if (ui_button(ui, CLAY_ID("NewGameCancel"), app_tr(ui->app, "label.cancel", "Cancel"), BUTTON_NORMAL, false, true)) {
        ui_close_dialog(ui);
    }
    if (ui_button(ui, CLAY_ID("NewGameStart"), app_tr(ui->app, "command.game.new", "Start game"), BUTTON_PRIMARY, false, true)) {
        command_execute(&ui->app->commands, SV("game.new"));
        ui_close_dialog(ui);
    }
}

void ui_dialogs(Ui *ui) {
    switch (ui->dialog) {
        case UI_DIALOG_NONE: break;
        case UI_DIALOG_SETTINGS: ui_modal(ui, ui_form(ui, "settings"), 600, NULL, NULL); break;
        case UI_DIALOG_NEW_GAME: ui_modal(ui, ui_form(ui, "new_game"), 520, new_game_footer, NULL); break;
    }
}
