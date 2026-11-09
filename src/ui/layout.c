// Builds the window from LayoutSpec (parsed from assets/data/layout.json).
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>

// ---- Parsing ---------------------------------------------------------------------------

static const struct {
    const char *name;
    LayoutItemKind kind;
} ITEM_TYPES[] = {
    { "separator", LAYOUT_ITEM_SEPARATOR },     { "spacer", LAYOUT_ITEM_SPACER },
    { "engine_status", LAYOUT_ITEM_ENGINE_STATUS }, { "theme_picker", LAYOUT_ITEM_THEME_PICKER },
    { "move_counter", LAYOUT_ITEM_MOVE_COUNTER },
};

static void parse_items(JsonValue array, LayoutItemVec *out) {
    for (isize i = 0; i < json_array_len(array); ++i) {
        JsonValue v = json_array_at(array, i);
        LayoutItem item = { .kind = LAYOUT_ITEM_COMMAND };
        StringView type = json_string(json_get(v, "type"), SV(""));
        for (i32 k = 0; k < (i32)countof(ITEM_TYPES); ++k) {
            if (sv_eq(type, sv(ITEM_TYPES[k].name))) item.kind = ITEM_TYPES[k].kind;
        }
        item.command = json_string(json_get(v, "command"), SV(""));
        item.icon = json_string(json_get(v, "icon"), SV(""));
        item.primary = sv_eq(json_string(json_get(v, "style"), SV("")), SV("primary"));
        if (item.kind == LAYOUT_ITEM_COMMAND && item.command.len == 0) continue;
        vec_push(out, item);
    }
}

// StringViews point into the JSON document, which the App keeps alive.
void layout_parse(LayoutSpec *spec, JsonValue root, f32 default_sidebar) {
    layout_free(spec);
    parse_items(json_get(root, "toolbar"), &spec->toolbar);
    parse_items(json_get(root, "navbar"), &spec->navbar);
    JsonValue sidebar = json_get(root, "sidebar");
    spec->sidebar_width = (f32)json_number(json_get(sidebar, "width"), default_sidebar);
    spec->sidebar_left = sv_eq(json_string(json_get(sidebar, "side"), SV("right")), SV("left"));
    JsonValue panels = json_get(sidebar, "panels");
    for (isize i = 0; i < json_array_len(panels); ++i) {
        JsonValue entry = json_array_at(panels, i);
        JsonValue tabs = json_get(entry, "tabs");
        if (json_is_array(tabs)) {
            SidebarEntry group = { .tab_start = (i32)spec->tab_panels.len };
            for (isize k = 0; k < json_array_len(tabs); ++k) vec_push(&spec->tab_panels, json_string(json_array_at(tabs, k), SV("")));
            group.tab_count = (i32)spec->tab_panels.len - group.tab_start;
            vec_push(&spec->panels, group);
        } else {
            vec_push(&spec->panels, (SidebarEntry){ .panel = json_string(entry, SV("")) });
        }
    }
}

void layout_free(LayoutSpec *spec) {
    vec_free(&spec->toolbar);
    vec_free(&spec->navbar);
    vec_free(&spec->panels);
    vec_free(&spec->tab_panels);
}

// ---- Toolbar widgets -----------------------------------------------------------------------

static void separator(Ui *ui) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(ui_px(ui, 22)) } },
           .backgroundColor = color_to_clay(ui->theme->colors.border) }) {}
}

static void spacer(void) {
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
}

static void engine_status_widget(Ui *ui) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    Color dot = c->text_muted;
    StringView text = app_tr(app, "label.no_engine", "No engine");
    switch (app->engine ? app->engine_status : ENGINE_STATUS_STOPPED) {
        case ENGINE_STATUS_STARTING:
            dot = (Color){ 230, 160, 40, 255 };
            text = app_tr(app, "label.engine_starting", "Engine starting…");
            break;
        case ENGINE_STATUS_READY:
            dot = (Color){ 67, 160, 71, 255 };
            text = string_sv(&app->engine_profile->name);
            break;
        case ENGINE_STATUS_FAILED:
            dot = c->danger;
            text = app_tr(app, "label.engine_failed", "Engine failed");
            break;
        case ENGINE_STATUS_STOPPED:
            break;
    }
    CLAY({ .id = CLAY_ID("EngineStatus"),
           .layout = { .childGap = ui_u16(ui, 6), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                       .padding = { ui_u16(ui, 8), ui_u16(ui, 8), 0, 0 } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 8)), CLAY_SIZING_FIXED(ui_px(ui, 8)) } },
               .backgroundColor = color_to_clay(dot), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {}
        ui_label(ui, text, FONT_UI, 13, c->text_muted);
        if (app->analysis_valid && app->analysis_enabled) {
            CLAY_TEXT(ui_fmt(ui, "· %d", app->analysis->total_visits), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
        }
    }
}

static void theme_picker(Ui *ui) {
    const ThemeColors *c = &ui->theme->colors;
    Clay_ElementId id = CLAY_ID("ThemePicker");
    CLAY({ .id = CLAY_ID("ThemePickerAnchor"), .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) } } }) {
        String label = string_format(SV_FMT "  ▼", SV_ARG(string_sv(&ui->theme->name)));
        if (ui_button(ui, id, string_sv(&label), BUTTON_NORMAL, false, true)) ui->theme_menu_open = !ui->theme_menu_open;
        string_free(&label);
        if (ui->theme_menu_open) {
            Clay_ElementId menu = CLAY_ID("ThemeMenu");
            CLAY({ .id = menu,
                   .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 100,
                                 .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM },
                                 .offset = { 0, ui_px(ui, 6) } },
                   .layout = { .sizing = { CLAY_SIZING_FIT(ui_px(ui, 180)), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                               .padding = CLAY_PADDING_ALL(ui_u16(ui, 6)), .childGap = ui_u16(ui, 2) },
                   .backgroundColor = color_to_clay(color_with_alpha(c->surface, 1.0f)),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius)),
                   .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                ui->pointer_over_menu = Clay_PointerOver(menu);
                for (isize i = 0; i < ui->themes.len; ++i) {
                    Clay_ElementId item = CLAY_IDI("ThemeItem", (u32)i);
                    bool current = i == ui->theme_index;
                    bool over = Clay_PointerOver(item);
                    CLAY({ .id = item,
                           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                                       .padding = { ui_u16(ui, 10), ui_u16(ui, 10), ui_u16(ui, 6), ui_u16(ui, 6) },
                                       .childGap = ui_u16(ui, 8), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                           .backgroundColor = color_to_clay(over ? c->hover : (Color){ 0 }),
                           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                        // Swatch: background + board colour of that theme.
                        const ThemeColors *tc = &ui->themes.items[i].colors;
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 22)), CLAY_SIZING_FIXED(ui_px(ui, 14)) } },
                               .backgroundColor = color_to_clay(tc->background),
                               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)),
                               .border = { .color = color_to_clay(tc->board), .width = CLAY_BORDER_OUTSIDE(3) } }) {}
                        ui_label(ui, string_sv(&ui->themes.items[i].name), current ? FONT_UI_BOLD : FONT_UI, 14, c->text);
                    }
                    if (over && ui->mouse_released) {
                        ui_apply_theme(ui, i);
                        ui->theme_menu_open = false;
                    }
                }
            }
            if (ui->mouse_pressed && !ui->pointer_over_menu && !Clay_PointerOver(id)) ui->theme_menu_open = false;
        }
    }
}

static void move_counter(Ui *ui) {
    const GameNode *n = ui->app->tree->current;
    GameNode *line[2048];
    isize total = game_tree_active_line(ui->app->tree, line, countof(line));
    i32 last = total > 0 ? line[MIN(total, (isize)countof(line)) - 1]->move_number : 0;
    i32 siblings = n->parent ? game_node_child_count(n->parent) : 1;
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .padding = { ui_u16(ui, 10), ui_u16(ui, 10), 0, 0 }, .childGap = ui_u16(ui, 6),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        CLAY_TEXT(ui_fmt(ui, SV_FMT " %d / %d", SV_ARG(app_tr(ui->app, "label.move", "Move")), n->move_number, last),
                  ui_text_style(ui, FONT_UI_BOLD, 14, c->text));
        if (siblings > 1) {
            CLAY_TEXT(ui_fmt(ui, "· " SV_FMT " %d/%d", SV_ARG(app_tr(ui->app, "label.variation", "Variation")),
                             game_node_sibling_index(n) + 1, siblings),
                      ui_text_style(ui, FONT_UI, 13, c->text_muted));
        }
    }
}

static void items(Ui *ui, const LayoutItemVec *list, bool ghost) {
    for (isize i = 0; i < list->len; ++i) {
        const LayoutItem *item = &list->items[i];
        switch (item->kind) {
            case LAYOUT_ITEM_COMMAND:
                ui_command_button(ui, item->command, item->icon, item->primary ? BUTTON_PRIMARY : ghost ? BUTTON_GHOST : BUTTON_NORMAL, (u32)i);
                break;
            case LAYOUT_ITEM_SEPARATOR: separator(ui); break;
            case LAYOUT_ITEM_SPACER: spacer(); break;
            case LAYOUT_ITEM_ENGINE_STATUS: engine_status_widget(ui); break;
            case LAYOUT_ITEM_THEME_PICKER: theme_picker(ui); break;
            case LAYOUT_ITEM_MOVE_COUNTER: move_counter(ui); break;
        }
    }
}

// ---- Window ------------------------------------------------------------------------------------

// Floating card over the top of the board when the current position has a score.
static void result_banner(Ui *ui) {
    App *app = ui->app;
    bool show = app->result.len && app->result_position == app->synced_position_id;
    f32 t = anim_towards(&ui->anim, 0x8E5017u, show ? 1.0f : 0.0f, 12.0f, 0.0f);
    if (t < 0.01f) return;
    const ThemeColors *c = &ui->theme->colors;
    StringView text = string_sv(&app->result);
    StringView winner = sv_starts_with(text, SV("B")) ? app_tr(app, "label.black", "Black")
                        : sv_starts_with(text, SV("W")) ? app_tr(app, "label.white", "White")
                                                        : SV("");
    CLAY({ .id = CLAY_ID("ResultBanner"),
           .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 50,
                         .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                         .offset = { 0, ui_px(ui, 24) - (1.0f - t) * ui_px(ui, 16) } },
           .layout = { .padding = { ui_u16(ui, 18), ui_u16(ui, 10), ui_u16(ui, 10), ui_u16(ui, 10) }, .childGap = ui_u16(ui, 12),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(color_with_alpha(c->surface, 0.96f * t)),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius)),
           .border = { .color = color_to_clay(color_with_alpha(c->border, t)), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        ui_label(ui, app_tr(app, "label.result", "Result"), FONT_UI, 14, color_with_alpha(c->text_muted, t));
        CLAY_TEXT(ui_fmt(ui, SV_FMT "%s" SV_FMT, SV_ARG(text), winner.len ? "  " : "", SV_ARG(winner)),
                  ui_text_style(ui, FONT_UI_BOLD, 18, color_with_alpha(c->text, t)));
        ui_command_button(ui, SV("game.write_result"), SV(""), BUTTON_NORMAL, 0);
    }
}

static Clay_ElementDeclaration bar(Ui *ui, Clay_ElementId id, bool top) {
    const Theme *t = ui->theme;
    Clay_BorderWidth border = { 0 };
    if (top) border.bottom = 1;
    else border.top = 1;
    return (Clay_ElementDeclaration){
        .id = id,
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .padding = { ui_u16(ui, 12), ui_u16(ui, 12), ui_u16(ui, 8), ui_u16(ui, 8) },
                    .childGap = ui_u16(ui, 6),
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
        .backgroundColor = color_to_clay(t->colors.surface),
        .border = { .color = color_to_clay(t->colors.border), .width = border },
    };
}

// Segmented tab bar + the selected panel. Selection is remembered per group.
static void tab_group(Ui *ui, const SidebarEntry *entry, i32 group) {
    const ThemeColors *c = &ui->theme->colors;
    i32 *selected = &ui->tab_selected[MIN(group, (i32)countof(ui->tab_selected) - 1)];
    *selected = CLAMP(*selected, 0, entry->tab_count - 1);
    // The group fills the rest of the sidebar; its content box has a fixed size
    // (overflow hidden) and each panel scrolls inside it, so switching tabs or
    // long content never moves anything else.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(ui_px(ui, 160)) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childGap = ui_u16(ui, 6) } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 2),
                           .padding = CLAY_PADDING_ALL(ui_u16(ui, 3)) },
               .backgroundColor = color_to_clay(c->surface),
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius)),
               .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE((u16)ui->theme->shape.border_width) } }) {
            for (i32 t = 0; t < entry->tab_count; ++t) {
                StringView name = ui->layout.tab_panels.items[entry->tab_start + t];
                // Short label: "tab.<panel>", then "panel.<panel>", then the panel id.
                char tab_key[96], panel_key[96];
                snprintf(tab_key, sizeof(tab_key), "tab." SV_FMT, SV_ARG(name));
                snprintf(panel_key, sizeof(panel_key), "panel." SV_FMT, SV_ARG(name));
                StringView label = app_tr(ui->app, tab_key, app_tr(ui->app, panel_key, panel_key + 6).data);
                Clay_ElementId id = CLAY_IDI("SidebarTab", (u32)(group * 32 + t));
                bool active = t == *selected, over = Clay_PointerOver(id);
                f32 on = anim_towards(&ui->anim, id.id, active ? 1.0f : 0.0f, 14.0f, active ? 1.0f : 0.0f);
                f32 hov = anim_towards(&ui->anim, id.id ^ 0x7AB5u, over ? 1.0f : 0.0f, 16.0f, 0.0f);
                Color tab_bg = color_lerp(color_with_alpha(c->hover, hov * c->hover.a / 255.0f), c->accent, on);
                CLAY({ .id = id,
                       .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                                   .padding = { ui_u16(ui, 4), ui_u16(ui, 4), ui_u16(ui, 5), ui_u16(ui, 5) },
                                   .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                       .backgroundColor = color_to_clay(tab_bg),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius * 0.6f)) }) {
                    CLAY_TEXT(ui_str(ui, label), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 13),
                                                                    .textColor = color_to_clay(color_lerp(c->text_muted, c->accent_text, on)),
                                                                    .wrapMode = CLAY_TEXT_WRAP_NONE }));
                }
                if (over && ui->mouse_pressed && !ui->pointer_over_menu) *selected = t;
            }
        }
        CLAY({ .id = CLAY_IDI("TabContent", (u32)group),
               // No clip here: clip containers stop Clay from compressing their
               // children, so only the panel's own scroll area clips.
               .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            ui->in_tab = true;
            panels_render(ui, ui->layout.tab_panels.items[entry->tab_start + *selected]);
            ui->in_tab = false;
        }
    }
}

// The sidebar is a scrolling viewport around a column. The column is exactly
// the viewport height when the window is tall enough (so a tab group can fill
// the remaining space with a fixed-size, internally scrolling box), and never
// shorter than its minimum, in which case the whole sidebar scrolls instead of
// pushing the navbar off-screen.
static void sidebar(Ui *ui) {
    const LayoutSpec *spec = &ui->layout;
    Clay_ElementId viewport = CLAY_ID("Sidebar");
    Clay_ElementData last = Clay_GetElementData(viewport);   // previous frame's size
    f32 available = last.found ? last.boundingBox.height : (f32)GetScreenHeight();
    f32 column_h = fmaxf(available, ui_px(ui, 640));
    f32 gap = ui_px(ui, ui->theme->shape.gap);
    CLAY({ .id = viewport,
           .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, spec->sidebar_width)), CLAY_SIZING_GROW(0) } },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        CLAY({ .id = CLAY_ID("SidebarColumn"),
               .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(column_h) },
                           .padding = CLAY_PADDING_ALL((u16)gap),
                           .childGap = (u16)gap,
                           .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            for (isize i = 0; i < spec->panels.len; ++i) {
                const SidebarEntry *e = &spec->panels.items[i];
                if (e->tab_count == 0) panels_render(ui, e->panel);
                else tab_group(ui, e, (i32)i);
            }
        }
    }
}

void layout_build(Ui *ui) {
    App *app = ui->app;
    const LayoutSpec *spec = &ui->layout;
    ui->pointer_over_menu = false;
    CLAY({ .id = CLAY_ID("Root"),
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        CLAY(bar(ui, CLAY_ID("Toolbar"), true)) { items(ui, &spec->toolbar, false); }
        CLAY({ .id = CLAY_ID("Main"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
            if (spec->sidebar_left) sidebar(ui);
            CLAY({ .id = CLAY_ID("BoardArea"),
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                               .padding = CLAY_PADDING_ALL(ui_u16(ui, ui->theme->shape.gap)) } }) {
                board_view_element(ui);
                result_banner(ui);
            }
            if (!spec->sidebar_left) sidebar(ui);
        }
        CLAY(bar(ui, CLAY_ID("Navbar"), false)) {
            items(ui, &spec->navbar, true);
            if (app->status_message.len && os_time_seconds() < app->status_until) {
                ui_label(ui, string_sv(&app->status_message), FONT_UI, 13, ui->theme->colors.text_muted);
            }
        }
        ui_dialogs(ui);
    }
}
