// "Customize layout": rearranges the sidebar (panel stack and tab group), its
// side and width, and the toolbar. Every change is written to the user's
// <config_dir>/layout.lisp right away and reloaded, so the window behind the
// dialog shows it live. "Undo changes" restores what was there when the dialog
// opened; "Reset to default" removes the user file.
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static String user_layout_path(const Ui *ui) { return path_join(string_sv(&ui->app->config_dir), SV("layout.lisp")); }

// (Re)reads the model from the layout file the app uses now.
static bool load_model(Ui *ui) {
    LayoutEditor *e = &ui->layout_editor;
    layout_model_free(&e->model);
    string_free(&e->source);
    if (!os_read_file(string_sv(&ui->layout_file), &e->source)) return false;
    const char *error = NULL;
    if (!layout_model_read(string_sv(&e->source), &e->model, &error)) {
        app_set_status(ui->app, "Customize layout: %s", error);
        return false;
    }
    return true;
}

// Writes the edited model as the user layout and reloads it.
static void apply(Ui *ui) {
    LayoutEditor *e = &ui->layout_editor;
    String text = layout_model_write(string_sv(&e->source), &e->model);
    String path = user_layout_path(ui);
    os_make_dirs(string_sv(&ui->app->config_dir));
    if (!os_write_file(string_sv(&path), string_sv(&text))) app_set_status(ui->app, "Could not write %s", string_cstr(&path));
    string_free(&path);
    string_free(&text);
    layout_load(ui);
    load_model(ui);   // positions refer to the new text
}

void layout_editor_open(Ui *ui) {
    LayoutEditor *e = &ui->layout_editor;
    if (!e->open) {
        String path = user_layout_path(ui);
        string_free(&e->original_user);
        e->had_user_file = os_read_file(string_sv(&path), &e->original_user);
        string_free(&path);
        if (!load_model(ui)) return;
        e->open = true;
    }
    ui->dialog = UI_DIALOG_LAYOUT_EDITOR;
    ui->focus_id = 0;
    anim_set(&ui->anim, CLAY_ID("ModalDialog").id, 0.0f);
}

void layout_editor_close(Ui *ui) {
    LayoutEditor *e = &ui->layout_editor;
    e->open = false;
    layout_model_free(&e->model);
    string_free(&e->source);
    string_free(&e->original_user);
}

static void undo_changes(Ui *ui) {
    LayoutEditor *e = &ui->layout_editor;
    String path = user_layout_path(ui);
    if (e->had_user_file) {
        if (!os_write_file(string_sv(&path), string_sv(&e->original_user))) app_set_status(ui->app, "Could not write %s", string_cstr(&path));
    } else {
        os_remove(string_sv(&path));
    }
    string_free(&path);
    layout_load(ui);
    load_model(ui);
}

static void reset_default(Ui *ui) {
    String path = user_layout_path(ui);
    os_remove(string_sv(&path));
    string_free(&path);
    layout_load(ui);
    load_model(ui);
    app_set_status(ui->app, "Layout reset to the default");
}

// ---- Widgets ---------------------------------------------------------------------------------------

static StringView panel_label(Ui *ui, StringView name) {
    char key[64];
    snprintf(key, sizeof(key), "panel." SV_FMT, SV_ARG(name));
    String fallback = string_from(name);
    StringView label = app_tr(ui->app, key, string_cstr(&fallback));
    StringView out = arena_format(&ui->frame, SV_FMT, SV_ARG(label));   // fallback is freed below
    string_free(&fallback);
    return out;
}

static StringView toolbar_label(Ui *ui, StringView item) {
    StringView cmd = layout_item_command(item);
    if (cmd.len) {
        const CommandDef *def = command_find(&ui->app->commands, cmd);
        char key[96];
        snprintf(key, sizeof(key), "command." SV_FMT, SV_ARG(cmd));
        StringView label = app_tr(ui->app, key, def ? def->label : "?");
        return arena_format(&ui->frame, SV_FMT, SV_ARG(label));
    }
    // (separator) (spacer) (engine-status) ...: show the element's name.
    StringView s = sv_trim(item);
    if (s.len > 2) s = sv_drop(s, 1);
    isize n = 0;
    while (n < s.len && s.data[n] != ' ' && s.data[n] != ')') ++n;
    return arena_format(&ui->frame, "— " SV_FMT " —", SV_ARG(sv_take(s, n)));
}

typedef enum RowAction { ROW_NONE, ROW_UP, ROW_DOWN, ROW_MOVE, ROW_REMOVE } RowAction;

// One list row: label, then up / down / move-to-other-list / remove buttons.
static RowAction row(Ui *ui, u32 id, StringView label, StringView move_label, bool can_up, bool can_down) {
    const ThemeColors *c = &ui->theme->colors;
    RowAction action = ROW_NONE;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4),
                       .padding = { ui_u16(ui, 8), ui_u16(ui, 4), ui_u16(ui, 3), ui_u16(ui, 3) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 5)) }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) { ui_label(ui, label, FONT_UI, 14, c->text); }
        if (ui_button(ui, CLAY_IDI("LayUp", id), SV("@up"), BUTTON_GHOST, false, can_up)) action = ROW_UP;
        if (ui_button(ui, CLAY_IDI("LayDown", id), SV("@down"), BUTTON_GHOST, false, can_down)) action = ROW_DOWN;
        if (move_label.len && ui_button(ui, CLAY_IDI("LayMove", id), move_label, BUTTON_GHOST, false, true)) action = ROW_MOVE;
        if (ui_button(ui, CLAY_IDI("LayRemove", id), SV("@close"), BUTTON_GHOST, false, true)) action = ROW_REMOVE;
    }
    return action;
}

static void column_title(Ui *ui, StringView title) {
    CLAY({ .layout = { .padding = { 0, 0, ui_u16(ui, 2), ui_u16(ui, 4) } } }) {
        CLAY_TEXT(ui_str(ui, title), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 13),
                                                        .textColor = color_to_clay(ui->theme->colors.text_muted), .letterSpacing = 1 }));
    }
}

static Clay_ElementDeclaration list_column(Ui *ui, Clay_ElementId id, f32 width) {
    return (Clay_ElementDeclaration){
        .id = id,
        .layout = { .sizing = { width > 0 ? CLAY_SIZING_FIXED(ui_px(ui, width)) : CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 5) },
        .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
    };
}

// ---- Dialog ---------------------------------------------------------------------------------------

static void body(Ui *ui, void *user) {
    UNUSED(user);
    LayoutEditor *e = &ui->layout_editor;
    LayoutModel *m = &e->model;
    const ThemeColors *c = &ui->theme->colors;
    bool changed = false;

    // Sidebar side and width.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        ui_label(ui, tr(ui, "layout_editor.side", "Sidebar"), FONT_UI, 14, c->text_muted);
        if (ui_button(ui, CLAY_ID("LaySideLeft"), tr(ui, "layout_editor.left", "Left"), BUTTON_NORMAL, m->sidebar_left, true) && !m->sidebar_left) {
            m->sidebar_left = true;
            changed = true;
        }
        if (ui_button(ui, CLAY_ID("LaySideRight"), tr(ui, "layout_editor.right", "Right"), BUTTON_NORMAL, !m->sidebar_left, true) && m->sidebar_left) {
            m->sidebar_left = false;
            changed = true;
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 24)), CLAY_SIZING_FIT(0) } } }) {}
        ui_label(ui, tr(ui, "layout_editor.width", "Width"), FONT_UI, 14, c->text_muted);
        if (ui_button(ui, CLAY_ID("LayNarrow"), SV("−"), BUTTON_NORMAL, false, m->sidebar_width > 280)) {
            m->sidebar_width = MAX(280, m->sidebar_width - 20);
            changed = true;
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 60)), CLAY_SIZING_FIT(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
            ui_label(ui, arena_format(&ui->frame, "%d", m->sidebar_width), FONT_MONO, 14, c->text);
        }
        if (ui_button(ui, CLAY_ID("LayWider"), SV("+"), BUTTON_NORMAL, false, m->sidebar_width < 720)) {
            m->sidebar_width = MIN(720, m->sidebar_width + 20);
            changed = true;
        }
    }

    f32 page_h = fminf(ui_px(ui, 520), (f32)GetScreenHeight() * 0.62f);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(page_h) }, .childGap = ui_u16(ui, 14) } }) {
        // Panels shown one above the other.
        CLAY(list_column(ui, CLAY_ID("LayStack"), 0)) {
            column_title(ui, tr(ui, "layout_editor.stack", "SIDEBAR PANELS"));
            for (isize i = 0; i < m->stack.len; ++i) {
                StringView item = string_sv(&m->stack.items[i]);
                StringView panel = layout_item_panel(item);
                RowAction a = row(ui, (u32)i, panel.len ? panel_label(ui, panel) : item, tr(ui, "layout_editor.to_tabs", "→ Tabs"),
                                  i > 0, i + 1 < m->stack.len);
                if (a == ROW_UP) layout_vec_move(&m->stack, i, -1), changed = true;
                if (a == ROW_DOWN) layout_vec_move(&m->stack, i, 1), changed = true;
                if (a == ROW_MOVE && panel.len) {
                    vec_push(&m->tabs, string_from(panel));
                    layout_vec_remove(&m->stack, i);
                    changed = true;
                }
                if (a == ROW_REMOVE) layout_vec_remove(&m->stack, i), changed = true;
                if (changed) break;
            }
        }
        // Panels in the tab group.
        CLAY(list_column(ui, CLAY_ID("LayTabs"), 0)) {
            column_title(ui, tr(ui, "layout_editor.tabs", "TABS"));
            for (isize i = 0; i < m->tabs.len && !changed; ++i) {
                StringView name = string_sv(&m->tabs.items[i]);
                RowAction a = row(ui, 100 + (u32)i, panel_label(ui, name), tr(ui, "layout_editor.to_sidebar", "← Sidebar"), i > 0, i + 1 < m->tabs.len);
                if (a == ROW_UP) layout_vec_move(&m->tabs, i, -1), changed = true;
                if (a == ROW_DOWN) layout_vec_move(&m->tabs, i, 1), changed = true;
                if (a == ROW_MOVE) {
                    vec_push(&m->stack, string_format("(panel " SV_FMT ")", SV_ARG(name)));
                    layout_vec_remove(&m->tabs, i);
                    changed = true;
                }
                if (a == ROW_REMOVE) layout_vec_remove(&m->tabs, i), changed = true;
            }
            // Hidden panels.
            column_title(ui, tr(ui, "layout_editor.hidden", "HIDDEN"));
            for (isize p = 0; p < LAYOUT_PANEL_COUNT && !changed; ++p) {
                StringView name = sv(LAYOUT_PANELS[p]);
                if (layout_model_shows(m, name)) continue;
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 4),
                                   .padding = { ui_u16(ui, 8), ui_u16(ui, 4), ui_u16(ui, 3), ui_u16(ui, 3) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                       .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) }, .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 5)) }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) { ui_label(ui, panel_label(ui, name), FONT_UI, 14, c->text_muted); }
                    if (ui_button(ui, CLAY_IDI("LayShowSide", (u32)p), tr(ui, "layout_editor.add_sidebar", "+ Sidebar"), BUTTON_GHOST, false, true)) {
                        vec_push(&m->stack, string_format("(panel " SV_FMT ")", SV_ARG(name)));
                        changed = true;
                    }
                    if (ui_button(ui, CLAY_IDI("LayShowTab", (u32)p), tr(ui, "layout_editor.add_tabs", "+ Tabs"), BUTTON_GHOST, false, true)) {
                        vec_push(&m->tabs, string_from(name));
                        changed = true;
                    }
                }
            }
        }
        // Toolbar buttons, and the commands that can be added.
        CLAY(list_column(ui, CLAY_ID("LayToolbar"), 0)) {
            column_title(ui, tr(ui, "layout_editor.toolbar", "TOOLBAR"));
            for (isize i = 0; i < m->toolbar.len && !changed; ++i) {
                RowAction a = row(ui, 200 + (u32)i, toolbar_label(ui, string_sv(&m->toolbar.items[i])), SV(""), i > 0, i + 1 < m->toolbar.len);
                if (a == ROW_UP) layout_vec_move(&m->toolbar, i, -1), changed = true;
                if (a == ROW_DOWN) layout_vec_move(&m->toolbar, i, 1), changed = true;
                if (a == ROW_REMOVE) layout_vec_remove(&m->toolbar, i), changed = true;
            }
            CLAY({ .layout = { .childGap = ui_u16(ui, 4) } }) {
                if (ui_button(ui, CLAY_ID("LayAddSep"), tr(ui, "layout_editor.add_separator", "+ Separator"), BUTTON_GHOST, false, true)) {
                    vec_push(&m->toolbar, string_from(SV("(separator)")));
                    changed = true;
                }
                if (ui_button(ui, CLAY_ID("LayAddSpacer"), tr(ui, "layout_editor.add_spacer", "+ Spacer"), BUTTON_GHOST, false, true)) {
                    vec_push(&m->toolbar, string_from(SV("(spacer)")));
                    changed = true;
                }
            }
        }
        CLAY(list_column(ui, CLAY_ID("LayCommands"), 230)) {
            column_title(ui, tr(ui, "layout_editor.add_button", "ADD A BUTTON"));
            vec_foreach(CommandDef, def, &ui->app->commands.commands) {
                if (changed) break;
                bool present = false;
                vec_foreach(String, item, &m->toolbar) present |= sv_eq(layout_item_command(string_sv(item)), sv(def->id));
                if (present) continue;
                char key[96];
                snprintf(key, sizeof(key), "command.%s", def->id);
                Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("LayCmd"), (u32)(def - ui->app->commands.commands.items));
                bool over = Clay_PointerOver(id);
                CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                                             .padding = { ui_u16(ui, 8), ui_u16(ui, 8), ui_u16(ui, 4), ui_u16(ui, 4) } },
                       .backgroundColor = color_to_clay(over ? c->hover : (Color){ 0 }), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                    ui_label(ui, SV("+"), FONT_UI_BOLD, 14, c->accent);
                    ui_label(ui, app_tr(ui->app, key, def->label), FONT_UI, 13, c->text);
                }
                if (over && ui->mouse_pressed) {
                    vec_push(&m->toolbar, string_format("(button %s)", def->id));
                    changed = true;
                }
            }
        }
    }
    if (changed) apply(ui);
}

static void footer(Ui *ui, void *user) {
    UNUSED(user);
    if (ui_button(ui, CLAY_ID("LayReset"), tr(ui, "layout_editor.reset", "Reset to default"), BUTTON_GHOST, false, true)) reset_default(ui);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
        ui_label(ui, tr(ui, "layout_editor.hint", "Changes apply right away and are saved to your layout.lisp."), FONT_UI, 12, ui->theme->colors.text_muted);
    }
    if (ui_button(ui, CLAY_ID("LayUndo"), tr(ui, "layout_editor.undo", "Undo changes"), BUTTON_NORMAL, false, true)) undo_changes(ui);
    if (ui_button(ui, CLAY_ID("LayDone"), tr(ui, "layout_editor.done", "Done"), BUTTON_PRIMARY, false, true)) ui_close_dialog(ui);
}

void layout_editor_dialog(Ui *ui) {
    if (!ui->layout_editor.open) {
        layout_editor_open(ui);
        if (!ui->layout_editor.open) {
            ui->dialog = UI_DIALOG_NONE;
            return;
        }
    }
    ui_modal_shell(ui, tr(ui, "layout_editor.title", "Customize layout"), 1180, body, footer, NULL);
}
