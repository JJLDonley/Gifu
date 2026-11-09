// Settings dialog, generated from assets/data/settings.json. Each field binds
// to a config key through the config field table; changes apply immediately.
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>

static StringView label_of(Ui *ui, JsonValue v) {
    const char *key = json_string(json_get(v, "label"), SV("")).data;
    StringView fallback = json_string(json_get(v, "fallback"), json_string(json_get(v, "key"), SV("?")));
    return key && *key ? i18n_get(&ui->app->i18n, sv(key), fallback) : fallback;
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

// ---- Rows ---------------------------------------------------------------------------------

static void field_label(Ui *ui, StringView label) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
        ui_label(ui, label, FONT_UI, 14, ui->theme->colors.text);
    }
}

static void bool_field(Ui *ui, JsonValue v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        field_label(ui, label_of(ui, v));
        if (ui_toggle(ui, CLAY_IDI("SettingToggle", index), config_get_bool(config, f))) {
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
        if (ui_button(ui, CLAY_IDI("SettingMinus", index), SV("−"), BUTTON_NORMAL, false, value > lo + 1e-9)) next = value - step;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 72)), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
            ui_label(ui, sv(text), FONT_MONO, 14, ui->theme->colors.text);
        }
        if (ui_button(ui, CLAY_IDI("SettingPlus", index), SV("+"), BUTTON_NORMAL, false, value < hi - 1e-9)) next = value + step;
        if (next != value) {
            config_set_number(config, f, CLAMP(round(next / step) * step, lo, hi));
            apply_change(ui, f);
        }
    }
}

typedef struct ChoiceOption {
    StringView value;
    StringView label;
    bool available;
} ChoiceOption;

static isize collect_options(Ui *ui, JsonValue v, ChoiceOption *out, isize cap) {
    StringView source = json_string(json_get(v, "source"), SV(""));
    isize n = 0;
    if (sv_eq(source, SV("themes"))) {
        for (isize i = 0; i < ui->themes.len && n < cap; ++i) {
            out[n++] = (ChoiceOption){ string_sv(&ui->themes.items[i].id), string_sv(&ui->themes.items[i].name), true };
        }
    } else if (sv_eq(source, SV("engines"))) {
        vec_foreach(EngineProfile, p, &ui->app->engine_profiles) {
            if (n < cap) out[n++] = (ChoiceOption){ string_sv(&p->id), string_sv(&p->name), engine_profile_available(p) };
        }
    } else {
        JsonValue options = json_get(v, "options");
        for (isize i = 0; i < json_array_len(options) && n < cap; ++i) {
            JsonValue o = json_array_at(options, i);
            StringView value = json_string(json_get(o, "value"), SV(""));
            out[n++] = (ChoiceOption){ value, json_string(json_get(o, "label"), value), true };
        }
    }
    return n;
}

static void choice_field(Ui *ui, JsonValue v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    ChoiceOption options[32];
    isize count = collect_options(ui, v, options, countof(options));
    i32 columns = (i32)CLAMP(json_int(json_get(v, "columns"), 3), 1, 6);
    StringView current = config_get_string(config, f);
    // An empty engine id means "first available": highlight what is running.
    if (sv_eq(sv(f->key), SV("engine")) && current.len == 0 && ui->app->engine_profile) current = string_sv(&ui->app->engine_profile->id);

    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 6) } }) {
        ui_label(ui, label_of(ui, v), FONT_UI, 14, c->text);
        for (isize row = 0; row * columns < count; ++row) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6) } }) {
                for (isize col = 0; col < columns; ++col) {
                    isize i = row * columns + col;
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(1.0f / (f32)columns - 0.02f), CLAY_SIZING_FIT(0) } } }) {
                        if (i >= count) continue;
                        bool selected = sv_eq(options[i].value, current);
                        Clay_ElementId id = CLAY_IDI("SettingChoice", index * 64 + (u32)i);
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                            if (ui_button(ui, id, options[i].label, BUTTON_NORMAL, selected, options[i].available) && !selected) {
                                config_set_string(config, f, options[i].value);
                                apply_change(ui, f);
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---- Dialog -------------------------------------------------------------------------------------

void settings_dialog(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    const Theme *t = ui->theme;
    f32 sw = (f32)GetScreenWidth(), sh = (f32)GetScreenHeight();
    Clay_ElementId backdrop = CLAY_ID("SettingsBackdrop");
    Clay_ElementId dialog = CLAY_ID("SettingsDialog");
    CLAY({ .id = backdrop,
           .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 200 },
           .layout = { .sizing = { CLAY_SIZING_FIXED(sw), CLAY_SIZING_FIXED(sh) },
                       .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(color_with_alpha(BLACK, t->dark ? 0.55f : 0.30f)) }) {
        CLAY({ .id = dialog,
               .layout = { .sizing = { CLAY_SIZING_FIXED(fminf(ui_px(ui, 600), sw - 40)), CLAY_SIZING_FIT(0, sh * 0.86f) },
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
                    ui_label(ui, app_tr(ui->app, "command.view.settings", "Settings"), FONT_UI_BOLD, 18, c->text);
                }
                if (ui_button(ui, CLAY_ID("SettingsClose"), SV("×"), BUTTON_GHOST, false, true)) ui->settings_open = false;
            }
            // Body (scrolls).
            CLAY({ .id = CLAY_ID("SettingsBody"),
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                               .padding = { ui_u16(ui, 20), ui_u16(ui, 20), ui_u16(ui, 12), ui_u16(ui, 20) },
                               .childGap = ui_u16(ui, 14) },
                   .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
                JsonValue sections = json_get(json_root(&ui->settings_doc), "sections");
                u32 index = 0;
                for (isize s = 0; s < json_array_len(sections); ++s) {
                    JsonValue section = json_array_at(sections, s);
                    StringView title_key = json_string(json_get(section, "title"), SV(""));
                    StringView title = i18n_get(&ui->app->i18n, title_key, json_string(json_get(section, "fallback"), title_key));
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                       .childGap = ui_u16(ui, 10) } }) {
                        ui_card_title(ui, title);
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
                        }
                    }
                }
            }
        }
    }
    bool over_dialog = Clay_PointerOver(dialog);
    ui->pointer_over_menu = true;   // nothing underneath reacts while the modal is open
    if (ui->mouse_pressed && !over_dialog) ui->settings_open = false;
}
