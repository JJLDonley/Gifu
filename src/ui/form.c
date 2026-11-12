// Data-driven forms and modal dialogs.
//
// assets/data/forms.lisp describes each form ("settings", "new_game", ...):
//   (form NAME :title KEY (section TITLE (TYPE CONFIG-KEY :props... (option V "L")...)...)...)
// Fields bind to config keys (the config field table) and apply immediately;
// dialogs add their own footer buttons around the form.
#include "ui/ui.h"
#include "platform/os.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static StringView field_key(const Sexpr *v) {
    const Sexpr *k = sexpr_next_arg(v, NULL);
    return k ? k->text : SV("");
}

static StringView label_of(Ui *ui, const Sexpr *v) {
    StringView key = sexpr_prop_text(v, "label", SV(""));
    StringView fallback = sexpr_prop_text(v, "fallback", field_key(v));
    return key.len ? i18n_get(&ui->app->i18n, key, fallback) : fallback;
}

// Sections/forms carry their title key as the first arg (sections) or :title (forms).
static StringView title_of(Ui *ui, const Sexpr *v) {
    StringView key = sexpr_prop_text(v, "title", SV(""));
    if (!key.len) {
        const Sexpr *arg = sexpr_next_arg(v, NULL);
        key = arg ? arg->text : SV("");
    }
    return i18n_get(&ui->app->i18n, key, sexpr_prop_text(v, "fallback", key));
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

static void bool_field(Ui *ui, const Sexpr *v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        field_label(ui, label_of(ui, v));
        if (ui_toggle(ui, CLAY_IDI("FormToggle", index), config_get_bool(config, f))) {
            config_set_bool(config, f, !config_get_bool(config, f));
            apply_change(ui, f);
        }
    }
}

// Splits a :format such as "%.0f s" into the number's own conversion ("%.0f") and
// the text after it (" s"), shown as a unit beside the input.
static void split_format(StringView format, char *number, isize number_cap, StringView *unit) {
    isize start = sv_find_char(format, '%'), end = start;
    if (start < 0) {
        snprintf(number, (usize)number_cap, "%%g");
        *unit = SV("");
        return;
    }
    while (end < format.len && !strchr("fgeFGE", format.data[end])) ++end;
    end = MIN(end + 1, format.len);
    snprintf(number, (usize)number_cap, SV_FMT, SV_ARG(sv_slice(format, start, end)));
    *unit = sv_trim(sv_drop(format, end));
}

// A typed number (digits and a point; Enter or a click elsewhere applies it), then its
// unit, then - / + (Shift: ten steps; the mouse wheel over the input steps too).
static void number_field(Ui *ui, const Sexpr *v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    const ThemeColors *c = &ui->theme->colors;
    f64 value = config_get_number(config, f);
    f64 step = sexpr_prop_number(v, "step", 1.0);
    f64 lo = sexpr_prop_number(v, "min", f->min), hi = sexpr_prop_number(v, "max", f->max);
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) step *= 10.0;
    char fmt[16];
    StringView unit;
    split_format(sexpr_prop_text(v, "format", SV("%g")), fmt, sizeof(fmt), &unit);
    // "Unset" (e.g. no clock, the engine's own value): its name instead of the unit.
    StringView zero_key = sexpr_prop_text(v, "zero", SV(""));
    f64 zero_value = sexpr_prop_number(v, "zero_value", 0.0);
    if (zero_key.len && fabs(value - zero_value) < 1e-9) {
        String key = string_from(zero_key);
        unit = arena_format(&ui->frame, SV_FMT, SV_ARG(app_tr(ui->app, string_cstr(&key), "default")));
        string_free(&key);
    }
    Clay_ElementId input = CLAY_IDI("FormNumber", index);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        field_label(ui, label_of(ui, v));
        f64 typed = value, next = value;
        bool was_typed = ui_number_input(ui, input, &typed, lo, hi, fmt, 60, 13, true);
        if (unit.len) ui_label(ui, unit, FONT_UI, 13, c->text_muted);   // "s", "no clock"...
        if (ui_button(ui, CLAY_IDI("FormMinus", index), SV("−"), BUTTON_NORMAL, false, value > lo + 1e-9)) next = value - step;
        if (ui_button(ui, CLAY_IDI("FormPlus", index), SV("+"), BUTTON_NORMAL, false, value < hi - 1e-9)) next = value + step;
        if (Clay_PointerOver(input) && ui->wheel != 0 && ui->focus_id != input.id) next = value + (ui->wheel > 0 ? step : -step);
        // A typed value is kept as typed; the buttons and the wheel snap to the step.
        if (was_typed) {
            config_set_number(config, f, CLAMP(typed, lo, hi));
            apply_change(ui, f);
        } else if (next != value) {
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

static isize collect_options(Ui *ui, const Sexpr *v, ChoiceOption *out, isize cap) {
    StringView source = sexpr_prop_text(v, "source", SV(""));
    isize n = 0;
    if (sv_eq(source, SV("themes"))) {
        for (isize i = 0; i < ui->themes.len && n < cap; ++i) {
            out[n++] = (ChoiceOption){ string_sv(&ui->themes.items[i].id), 0, string_sv(&ui->themes.items[i].name), true };
        }
    } else if (sv_eq(source, SV("engines"))) {
        vec_foreach(EngineProfile, p, &ui->app->engine_profiles) {
            if (n < cap) out[n++] = (ChoiceOption){ string_sv(&p->id), 0, string_sv(&p->name), engine_profile_available(p) };
        }
    } else if (sv_eq(source, SV("opponents"))) {
        // The engine that plays against you, by name: the one that analyses (""), or another profile.
        StringView analysing = ui->app->main.profile ? string_sv(&ui->app->main.profile->name) : SV("KataGo");
        out[n++] = (ChoiceOption){ SV(""), 0, analysing, true };
        vec_foreach(EngineProfile, p, &ui->app->engine_profiles) {
            if (ui->app->main.profile && sv_eq(string_sv(&p->id), string_sv(&ui->app->main.profile->id))) continue;   // listed first
            if (n < cap) out[n++] = (ChoiceOption){ string_sv(&p->id), 0, string_sv(&p->name), engine_profile_available(p) };
        }
    } else if (sv_eq(source, SV("networks")) || sv_eq(source, SV("human_networks"))) {
        // The networks in katago/networks (or its human/ folder); the one in use is highlighted.
        String katago = app_katago_dir(ui->app);
        String dir = path_join(string_sv(&katago), sv_eq(source, SV("networks")) ? SV("networks") : SV("networks/human"));
        StringVec names = { 0 };
        os_list_dir(string_sv(&dir), &names);
        vec_foreach(String, name, &names) {
            StringView nm = string_sv(name);
            if (n >= cap || !(sv_ends_with(nm, SV(".bin.gz")) || sv_ends_with(nm, SV(".bin")) || sv_ends_with(nm, SV(".txt.gz")))) continue;
            StringView value = arena_format(&ui->frame, SV_FMT, SV_ARG(nm));
            StringView label = value;
            if (sv_starts_with(label, SV("kata1-"))) label = sv_drop(label, 6);
            if (sv_ends_with(label, SV(".bin.gz")) || sv_ends_with(label, SV(".txt.gz"))) label = sv_take(label, label.len - 7);
            out[n++] = (ChoiceOption){ value, 0, label, true };
        }
        string_vec_free(&names);
        string_free(&dir);
        string_free(&katago);
    } else {
        // (option VALUE "Label")
        for (const Sexpr *o = sexpr_next_child(v, NULL); o && n < cap; o = sexpr_next_child(v, o)) {
            if (!sexpr_is_list(o, "option")) continue;
            const Sexpr *value = sexpr_next_arg(o, NULL);
            if (!value) continue;
            const Sexpr *label = sexpr_next_arg(o, value);
            // Human-like play needs KataGo's human network (Engine -> Download engines).
            bool available = !(sv_eq(field_key(v), SV("ai.style")) && sv_eq(value->text, SV("human")) && !ui->app->human_available);
            out[n++] = (ChoiceOption){ value->text, value->number, label ? label->text : value->text, available };
        }
    }
    return n;
}

static void choice_field(Ui *ui, const Sexpr *v, const ConfigField *f, u32 index) {
    Config *config = &ui->app->config;
    ChoiceOption options[32];
    isize count = collect_options(ui, v, options, countof(options));
    i32 columns = (i32)CLAMP(sexpr_prop_number(v, "columns", 3), 1, 6);
    bool numeric = f->type != CONFIG_STRING;
    StringView current = config_get_string(config, f);
    f64 current_number = config_get_number(config, f);
    // An empty engine id means "first available": highlight what is running.
    if (sv_eq(sv(f->key), SV("engine")) && current.len == 0 && ui->app->main.profile) current = string_sv(&ui->app->main.profile->id);
    // No network chosen yet: highlight the one the running engine loaded.
    const EngineProfile *running = ui->app->main.profile;
    if (sv_eq(sv(f->key), SV("katago.network")) && current.len == 0 && running) current = path_basename(string_sv(&running->model));
    if (sv_eq(sv(f->key), SV("katago.human_network")) && current.len == 0 && running) current = path_basename(string_sv(&running->human_model));

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
                                // The old value was freed: the remaining options compare against the new one.
                                current = config_get_string(config, f);
                                current_number = config_get_number(config, f);
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

static void text_field(Ui *ui, const Sexpr *v, const ConfigField *f, u32 index) {
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
        TextInputResult r = ui_text_input(ui, id, edit, 14, 1, sexpr_prop_text(v, "placeholder", SV("")));
        if (r == TEXT_INPUT_CHANGED || r == TEXT_INPUT_COMMITTED) {
            config_set_string(&ui->app->config, f, string_sv(&edit->text));
            apply_change(ui, f);
        }
    }
}

// ---- Forms ----------------------------------------------------------------------------------------

const Sexpr *ui_form(Ui *ui, const char *name) {
    if (!ui->forms_root) return NULL;
    for (const Sexpr *f = ui->forms_root->first; f; f = f->next) {
        const Sexpr *id = sexpr_is_list(f, "form") ? sexpr_next_arg(f, NULL) : NULL;
        if (id && sv_eq(id->text, sv(name))) return f;
    }
    return NULL;
}

// (field ... :when "key=value,key2=value2|key3=value3"): shown while every setting
// of one of the |-separated groups has its value ("key=" = empty). Numbers compare
// by value.
static bool when_group(const Ui *ui, StringView rest) {
    while (rest.len) {
        StringView cond = sv_trim(sv_chop_char(&rest, ','));
        isize eq = sv_find(cond, SV("="));
        if (eq < 0) continue;
        const ConfigField *f = config_field(sv_trim(sv_take(cond, eq)));
        StringView want = sv_trim(sv_drop(cond, eq + 1));
        if (!f) return false;
        if (f->type == CONFIG_STRING) {
            if (!sv_eq(config_get_string(&ui->app->config, f), want)) return false;
        } else {
            f64 n = 0;
            if (!sv_parse_f64(want, &n) || fabs(config_get_number(&ui->app->config, f) - n) > 1e-9) return false;
        }
    }
    return true;
}

static bool field_visible(const Ui *ui, const Sexpr *v) {
    StringView when = sexpr_prop_text(v, "when", SV(""));
    if (!when.len) return true;
    while (when.len) {
        if (when_group(ui, sv_chop_char(&when, '|'))) return true;
    }
    return false;
}

// (keymap): every command with its keys; Change / + wait for the next key
// (ui.c capture_key), Clear removes them. Saved at once to <config_dir>/keymap.kv.
static void keymap_field(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    ui_label(ui, app_tr(app, "keys.help", "Change, then press the new key (with Ctrl / Shift / Alt if you like). Esc cancels."), FONT_UI, 13,
             c->text_muted);
    u32 i = 0;
    vec_foreach(CommandDef, cmd, &app->commands.commands) {
        ++i;
        StringView id = sv(cmd->id);
        char key[128];
        snprintf(key, sizeof(key), "command.%s", cmd->id);
        StringView label = app_tr(app, key, cmd->label);
        String keys = { 0 };
        vec_foreach(KeyBinding, b, &app->keymap.bindings) {
            if (!sv_eq(string_sv(&b->command), id)) continue;
            char text[64];
            key_chord_format(b->chord, text, (i32)sizeof(text));
            string_pushf(&keys, "%s%s", keys.len ? ", " : "", text);
        }
        bool capturing = sv_eq(sv(ui->key_capture), id);
        StringView shown = capturing ? app_tr(app, "keys.press", "Press a key…")
                           : keys.len ? arena_format(&ui->frame, "%s", string_cstr(&keys))
                                      : app_tr(app, "keys.none", "—");
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                           .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
            field_label(ui, label);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 150)), CLAY_SIZING_FIT(0) } } }) {
                ui_label(ui, shown, FONT_MONO, 13, capturing ? c->accent : keys.len ? c->text : c->text_muted);
            }
            if (ui_button(ui, CLAY_IDI("KeyChange", i), app_tr(app, "keys.change", "Change"), BUTTON_SMALL, capturing && !ui->key_capture_add, true)) {
                snprintf(ui->key_capture, sizeof(ui->key_capture), "%s", cmd->id);
                ui->key_capture_add = false;
            }
            if (ui_button(ui, CLAY_IDI("KeyAdd", i), SV("+"), BUTTON_SMALL, capturing && ui->key_capture_add, keys.len > 0)) {
                snprintf(ui->key_capture, sizeof(ui->key_capture), "%s", cmd->id);
                ui->key_capture_add = true;
            }
            if (ui_button(ui, CLAY_IDI("KeyClear", i), app_tr(app, "keys.clear", "Clear"), BUTTON_SMALL, false, keys.len > 0)) {
                app_keymap_clear(app, id);
            }
        }
        string_free(&keys);
    }
}

static void form_section(Ui *ui, const Sexpr *section, u32 index, bool title) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 10) } }) {
        if (title) ui_card_title(ui, title_of(ui, section));
        for (const Sexpr *v = sexpr_next_child(section, NULL); v; v = sexpr_next_child(section, v), ++index) {
            StringView type = sexpr_head(v);
            if (sv_eq(type, SV("command"))) {   // (command ID): a button running a registered command
                ui_command_button_ex(ui, field_key(v), SV(""), true, BUTTON_NORMAL, 100 + index);
                continue;
            }
            if (sv_eq(type, SV("keymap"))) {   // (keymap): the key bindings editor
                keymap_field(ui);
                continue;
            }
            if (!field_visible(ui, v)) continue;
            const ConfigField *f = config_field(field_key(v));
            if (!f) {
                ui_label(ui, arena_format(&ui->frame, "unknown setting '" SV_FMT "'", SV_ARG(field_key(v))), FONT_UI, 12, c->danger);
                continue;
            }
            if (sv_eq(type, SV("bool"))) bool_field(ui, v, f, index);
            else if (sv_eq(type, SV("number"))) number_field(ui, v, f, index);
            else if (sv_eq(type, SV("choice"))) choice_field(ui, v, f, index);
            else if (sv_eq(type, SV("text"))) text_field(ui, v, f, index);
        }
    }
}

void form_render(Ui *ui, const Sexpr *form) {
    const ThemeColors *c = &ui->theme->colors;
    // Field indices stay stable per field (ids for buttons / text edits), tabbed or not.
    u32 base[32] = { 0 };
    const Sexpr *sections[32];
    i32 count = 0;
    u32 index = 0;
    for (const Sexpr *section = sexpr_next_child(form, NULL); section && count < 32; section = sexpr_next_child(form, section)) {
        if (!sexpr_is_list(section, "section")) continue;
        sections[count] = section;
        base[count++] = index;
        for (const Sexpr *v = sexpr_next_child(section, NULL); v; v = sexpr_next_child(section, v)) ++index;
    }
    if (!sexpr_flag(form, "tabs")) {
        for (i32 i = 0; i < count; ++i) form_section(ui, sections[i], base[i], true);
        return;
    }
    // Tabs: one section at a time in a fixed-height page (the dialog never jumps).
    i32 tab = CLAMP(ui->form_tab, 0, count - 1);
    // The tabs wrap onto more rows when they do not fit (narrow windows, long
    // translations) instead of stretching the dialog past its edge.
    Clay_ElementData body = Clay_GetElementData(CLAY_ID("ModalBody"));
    f32 avail = body.found ? body.boundingBox.width - ui_px(ui, 40) : 1e9f;   // the body's side padding
    f32 gap = ui_px(ui, 4);
    i32 row_start[33], rows = 0;
    f32 x = 0;
    for (i32 i = 0; i < count; ++i) {
        f32 w = font_measure(ui->fonts, FONT_UI_BOLD, ui_px(ui, 14), title_of(ui, sections[i])).x + 2.0f * ui_px(ui, 10);
        if (i == 0 || (x > 0 && x + gap + w > avail)) row_start[rows++] = i, x = w;
        else x += gap + w;
    }
    row_start[rows] = count;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 4), .padding = { 0, 0, 0, ui_u16(ui, 6) } },
           .border = { .color = color_to_clay(c->border), .width = { .bottom = 1 } } }) {
        for (i32 r = 0; r < rows; ++r) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4) } }) {
                for (i32 i = row_start[r]; i < row_start[r + 1]; ++i) {
                    if (ui_button(ui, CLAY_IDI("FormTab", (u32)i), title_of(ui, sections[i]), BUTTON_GHOST, i == tab, true)) ui->form_tab = i;
                }
            }
        }
    }
    // As tall as the window comfortably allows; longer pages scroll (with a scrollbar).
    f32 page_h = fminf(ui_px(ui, 600), (f32)GetScreenHeight() * 0.62f);
    Clay_ElementId page = CLAY_ID("FormPage");
    CLAY({ .id = page,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(page_h) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .padding = { 0, ui_u16(ui, 12), 0, 0 } },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        form_section(ui, sections[tab], base[tab], false);
        ui_scrollbar(ui, page, 200);   // the modal layer
    }
}

static void form_body(Ui *ui, void *user) { form_render(ui, (const Sexpr *)user); }

void ui_modal(Ui *ui, const Sexpr *form, f32 width, ModalSection footer, void *user) {
    UNUSED(user);
    if (!form) return;
    ui_modal_shell(ui, title_of(ui, form), width, form_body, footer, (void *)form);
}

void ui_modal_shell(Ui *ui, StringView title, f32 width, ModalSection body, ModalSection footer, void *user) {
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
                    ui_label(ui, title, FONT_UI_BOLD, 18, c->text);
                }
                if (ui_button(ui, CLAY_ID("ModalClose"), SV("@close"), BUTTON_GHOST, false, true)) ui_close_dialog(ui);
            }
            // Body (scrolls).
            CLAY({ .id = CLAY_ID("ModalBody"),
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                               .padding = { ui_u16(ui, 20), ui_u16(ui, 20), ui_u16(ui, 12), ui_u16(ui, 20) },
                               .childGap = ui_u16(ui, 14) },
                   .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
                body(ui, user);
                ui_scrollbar(ui, CLAY_ID("ModalBody"), 200);   // when the window is too short for the dialog
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
    if (ui->dialog == UI_DIALOG_SETTINGS) app_apply_engine_settings(ui->app);   // nothing left half-applied
    if (ui->dialog == UI_DIALOG_THEME_EDITOR) theme_editor_cancel(ui);          // closed without Save
    if (ui->dialog == UI_DIALOG_LAYOUT_EDITOR) layout_editor_close(ui);         // changes are already saved
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

// Engine settings (profile, KataGo threads/visits/GPU...) restart KataGo, which
// takes a moment: they are applied together with this button (or on close).
static void settings_footer(Ui *ui, void *user) {
    UNUSED(user);
    const ThemeColors *c = &ui->theme->colors;
    bool pending = app_engine_settings_pending(ui->app);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
        if (pending) ui_label(ui, app_tr(ui->app, "settings.restart_pending", "Engine settings changed: Apply restarts KataGo"), FONT_UI, 13, c->text_muted);
    }
    if (ui_button(ui, CLAY_ID("SettingsApply"), app_tr(ui->app, "label.apply", "Apply"), BUTTON_PRIMARY, false, pending)) {
        app_apply_engine_settings(ui->app);
    }
}

static void game_info_footer(Ui *ui, void *user) {
    UNUSED(user);
    if (ui_button(ui, CLAY_ID("GameInfoCancel"), app_tr(ui->app, "label.cancel", "Cancel"), BUTTON_NORMAL, false, true)) {
        ui_close_dialog(ui);
    }
    if (ui_button(ui, CLAY_ID("GameInfoApply"), app_tr(ui->app, "label.apply", "Apply"), BUTTON_PRIMARY, false, true)) {
        app_game_info_apply(ui->app);
        ui_close_dialog(ui);
    }
}

void ui_dialogs(Ui *ui) {
    switch (ui->dialog) {
        case UI_DIALOG_NONE: break;
        case UI_DIALOG_SETTINGS: ui_modal(ui, ui_form(ui, "settings"), 600, settings_footer, NULL); break;
        case UI_DIALOG_NEW_GAME: ui_modal(ui, ui_form(ui, "new_game"), 520, new_game_footer, NULL); break;
        case UI_DIALOG_SETUP: engine_setup_dialog(ui); break;
        case UI_DIALOG_GAME_INFO: ui_modal(ui, ui_form(ui, "game_info"), 520, game_info_footer, NULL); break;
        case UI_DIALOG_KOMI: ui_modal(ui, ui_form(ui, "komi"), 360, game_info_footer, NULL); break;
        case UI_DIALOG_REPORT: deep_report_dialog(ui); break;
        case UI_DIALOG_THEME_EDITOR: theme_editor_dialog(ui); break;
        case UI_DIALOG_LAYOUT_EDITOR: layout_editor_dialog(ui); break;
        case UI_DIALOG_UPDATE: update_dialog(ui); break;
        case UI_DIALOG_PROFILE: ui_modal_shell(ui, app_tr(ui->app, "profile.title", "My profile"), 940, profile_dialog_body, NULL, NULL); break;
    }
}
