// Layout: a tree of components read from assets/data/layout.lisp (or the
// user's <config_dir>/layout.lisp, hot-reloaded on change).
//
//   (name :key value :flag positional-args... (child ...) ...)
//
// Each component name maps to a render function in COMPONENTS[]. Containers
// lay children out flex-style: children stretch across the cross axis, take
// their natural size along the main axis, or share leftover space with :grow.
// Common props: :grow :width N :height N :gap N :padding N.
#include "ui/ui.h"

#include "base/log.h"
#include "base/os.h"

#include <math.h>
#include <stdio.h>

typedef struct LayoutCtx {
    bool horizontal;   // parent's main axis
    bool ghost;        // buttons render ghost-style (navbar)
    u32 counter;       // unique index for element ids
    i32 tabs_index;    // stable per-tabs-node state slot
    i32 menu_index;
} LayoutCtx;

typedef void (*ComponentFn)(Ui *ui, const Sexpr *node, LayoutCtx *ctx);
static void render_node(Ui *ui, const Sexpr *node, LayoutCtx *ctx);
static void render_children(Ui *ui, const Sexpr *node, LayoutCtx *ctx);

// ---- Sizing --------------------------------------------------------------------------------------

static Clay_Sizing node_sizing(Ui *ui, const Sexpr *node, const LayoutCtx *ctx, bool stretch_cross) {
    bool grow = sexpr_flag(node, "grow");
    f64 w = sexpr_prop_number(node, "width", 0), h = sexpr_prop_number(node, "height", 0);
    Clay_SizingAxis main = grow ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0);
    Clay_SizingAxis cross = stretch_cross ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0);
    Clay_Sizing s = ctx->horizontal ? (Clay_Sizing){ main, cross } : (Clay_Sizing){ cross, main };
    if (node == ui->sidebar_column && w > 0) w = layout_drag_width(ui, (f32)w);   // live while dragging the divider
    if (w > 0) s.width = CLAY_SIZING_FIXED(ui_px(ui, (f32)w));
    if (h > 0) s.height = CLAY_SIZING_FIXED(ui_px(ui, (f32)h));
    return s;
}

static u16 prop_px(Ui *ui, const Sexpr *node, const char *key, f32 fallback) {
    return ui_u16(ui, (f32)sexpr_prop_number(node, key, fallback));
}

// ---- Leaf widgets ------------------------------------------------------------------------------------

static void separator(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(node);
    Clay_Sizing s = ctx->horizontal ? (Clay_Sizing){ CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(ui_px(ui, 22)) }
                                    : (Clay_Sizing){ CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) };
    CLAY({ .layout = { .sizing = s }, .backgroundColor = color_to_clay(ui->theme->colors.border) }) {}
}

static void spacer(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(ui), UNUSED(node);
    CLAY({ .layout = { .sizing = ctx->horizontal ? (Clay_Sizing){ CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }
                                                 : (Clay_Sizing){ CLAY_SIZING_FIT(0), CLAY_SIZING_GROW(0) } } }) {}
}

static void button(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    const Sexpr *cmd = sexpr_next_arg(node, NULL);
    if (!cmd) return;
    StringView style = sexpr_prop_text(node, "style", SV(""));
    ButtonStyle bs = sv_eq(style, SV("primary")) ? BUTTON_PRIMARY : sv_eq(style, SV("ghost")) || ctx->ghost ? BUTTON_GHOST : BUTTON_NORMAL;
    ui_command_button_ex(ui, cmd->text, sexpr_prop_text(node, "icon", SV("")), sexpr_flag(node, "label"), bs, ctx->counter++);
}

static void status_text(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(node), UNUSED(ctx);
    App *app = ui->app;
    if (app->status_message.len && os_time_seconds() < app->status_until) {
        ui_label(ui, string_sv(&app->status_message), FONT_UI, 13, ui->theme->colors.text_muted);
    }
}

static void engine_status_widget(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(node), UNUSED(ctx);
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    Color dot = c->text_muted;
    StringView text = app_tr(app, "label.no_engine", "No engine");
    switch (app->main.engine ? app->main.status : ENGINE_STATUS_STOPPED) {
        case ENGINE_STATUS_STARTING:
            dot = (Color){ 230, 160, 40, 255 };
            text = app_tr(app, "label.engine_starting", "Engine starting…");
            break;
        case ENGINE_STATUS_READY:
            dot = (Color){ 67, 160, 71, 255 };
            text = string_sv(&app->main.profile->name);
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
        if (app_game_analysis_running(app)) {
            isize done = 0, total = 0;
            app_game_analysis_progress(app, &done, &total);
            CLAY_TEXT(ui_fmt(ui, "· %td/%td", done, total), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
        } else if (app->analysis_valid && app->analysis_enabled) {
            CLAY_TEXT(ui_fmt(ui, "· %d", app->analysis->total_visits), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
        }
    }
}

static void theme_picker(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(node), UNUSED(ctx);
    const ThemeColors *c = &ui->theme->colors;
    Clay_ElementId id = CLAY_ID("ThemePicker");
    CLAY({ .id = CLAY_ID("ThemePickerAnchor"), .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) } } }) {
        String label = string_format("@chevron_down " SV_FMT, SV_ARG(string_sv(&ui->theme->name)));
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

static void move_counter(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(node), UNUSED(ctx);
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

// ---- Board -------------------------------------------------------------------------------------------

// Floating card over the top of the board when the current position has a score.
static void result_banner(Ui *ui) {
    App *app = ui->app;
    bool show = app->result.len && app->result_position == app->main.synced_position_id;
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

// ---- Menu bar -------------------------------------------------------------------------------------


static void board(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    CLAY({ .id = CLAY_ID("BoardArea"),
           .layout = { .sizing = node_sizing(ui, node, ctx, true), .padding = CLAY_PADDING_ALL(prop_px(ui, node, "padding", ui->theme->shape.gap)) } }) {
        board_view_element(ui);
        result_banner(ui);
    }
}

// ---- Panels & tabs ---------------------------------------------------------------------------------------

static void panel(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(ctx);
    const Sexpr *name = sexpr_next_arg(node, NULL);
    if (!name) return;
    ui->panel_node = node;   // panels may read their own props (e.g. :height)
    panels_render(ui, name->text);
    ui->panel_node = NULL;
}

// Segmented tab bar + the selected panel in a box that fills the space it is
// given (overflow hidden; each panel scrolls inside). Selection is remembered
// per tabs node.
static void tabs(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    const ThemeColors *c = &ui->theme->colors;
    i32 slot = MIN(ctx->tabs_index, (i32)countof(ui->tab_selected) - 1);   // no side effects inside MIN
    ctx->tabs_index++;
    StringView names[16];
    i32 count = 0;
    for (const Sexpr *a = sexpr_next_arg(node, NULL); a && count < (i32)countof(names); a = sexpr_next_arg(node, a)) names[count++] = a->text;
    if (count == 0) return;
    bool sidebar = ui->in_sidebar_tabs;
    if (sidebar) ui->sidebar_tabs_slot = slot, ui->sidebar_tab_count = count;
    i32 *selected = &ui->tab_selected[slot];
    *selected = ((*selected % count) + count) % count;   // view.next_tab wraps
    Clay_Sizing sizing = node_sizing(ui, node, ctx, true);
    if (sexpr_flag(node, "grow") && !ctx->horizontal) sizing.height = CLAY_SIZING_GROW(ui_px(ui, 160));
    CLAY({ .layout = { .sizing = sizing, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 6) } }) {
        CLAY({ .id = CLAY_IDI("SidebarTabBar", (u32)slot),
               .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 2),
                           .padding = CLAY_PADDING_ALL(ui_u16(ui, 3)) },
               .backgroundColor = color_to_clay(c->surface),
               .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius)),
               .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE((u16)ui->theme->shape.border_width) } }) {
            for (i32 t = 0; t < count; ++t) {
                // Short label: "tab.<panel>", then "panel.<panel>", then the panel id.
                char tab_key[96], panel_key[96];
                snprintf(tab_key, sizeof(tab_key), "tab." SV_FMT, SV_ARG(names[t]));
                snprintf(panel_key, sizeof(panel_key), "panel." SV_FMT, SV_ARG(names[t]));
                StringView label = app_tr(ui->app, tab_key, app_tr(ui->app, panel_key, panel_key + 6).data);
                Clay_ElementId id = CLAY_IDI("SidebarTab", (u32)(slot * 32 + t));
                bool active = t == *selected, over = Clay_PointerOver(id);
                f32 on = anim_towards(&ui->anim, id.id, active ? 1.0f : 0.0f, 14.0f, active ? 1.0f : 0.0f);
                f32 hov = anim_towards(&ui->anim, id.id ^ 0x7AB5u, over ? 1.0f : 0.0f, 16.0f, 0.0f);
                Color tab_bg = color_lerp(color_with_alpha(c->hover, hov * c->hover.a / 255.0f), c->accent, on);
                CLAY({ .id = id,
                       .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                                   .padding = { ui_u16(ui, 4), ui_u16(ui, 4), ui_u16(ui, 5), ui_u16(ui, 5) },
                                   .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                       .backgroundColor = color_to_clay(tab_bg),
                       .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius * 0.6f)),
                       .clip = { .horizontal = true } }) {   // many tabs: shrink (clip) instead of widening the sidebar
                    CLAY_TEXT(ui_str(ui, label), CLAY_TEXT_CONFIG({ .fontId = FONT_UI_BOLD, .fontSize = ui_u16(ui, 12),
                                                                    .textColor = color_to_clay(color_lerp(c->text_muted, c->accent_text, on)),
                                                                    .wrapMode = CLAY_TEXT_WRAP_NONE }));
                }
                if (over && ui->mouse_pressed && !ui->pointer_over_menu) {
                    *selected = t;
                    if (sidebar) layout_drag_tab(ui, t, label);   // becomes a drag once the mouse moves
                }
            }
        }
        // No clip here: clip containers stop Clay from compressing their
        // children, so only the panel's own scroll area clips.
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            ui->in_tab = true;
            panels_render(ui, names[*selected]);
            ui->in_tab = false;
        }
    }
}

// ---- Containers ------------------------------------------------------------------------------------------

// The sidebar column's children: each stack item in a wrapper (drag source and
// drop reference), a resize handle under items with a :height, and the tabs.
static void render_sidebar_children(Ui *ui, const Sexpr *node, LayoutCtx *ctx, f32 gap) {
    isize index = 0;
    ui->sidebar_gap = gap;
    for (const Sexpr *c = sexpr_next_child(node, NULL); c; c = sexpr_next_child(node, c)) {
        if (sexpr_is_list(c, "tabs")) {
            ui->in_sidebar_tabs = true;
            render_node(ui, c, ctx);
            ui->in_sidebar_tabs = false;
            continue;
        }
        Clay_ElementId id = CLAY_IDI("SidebarItem", (u32)index);
        f64 height = sexpr_prop_number(c, "height", 0);
        Clay_SizingAxis h = sexpr_flag(c, "grow") ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0);
        CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_GROW(0), h }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            ui->panel_stack_index = index;
            render_node(ui, c, ctx);
            ui->panel_stack_index = -1;
            if (height > 0 && sexpr_is_list(c, "panel")) layout_drag_height_handle(ui, index, (f32)height, gap);
        }
        layout_drag_stack_item(ui, id, index, c);
        index++;
    }
    ui->sidebar_stack_count = index;
}

static const Sexpr *child_list(const Sexpr *node, const char *head) {
    for (const Sexpr *c = sexpr_next_child(node, NULL); c; c = sexpr_next_child(node, c)) {
        if (sexpr_is_list(c, head)) return c;
    }
    return NULL;
}

static void render_container_children(Ui *ui, const Sexpr *node, LayoutCtx *ctx, f32 gap) {
    if (node == ui->sidebar_column) {
        render_sidebar_children(ui, node, ctx, gap);
        return;
    }
    // The row holding the board and the sidebar (a column with tabs): a width divider between them.
    const Sexpr *board_node = ctx->horizontal ? child_list(node, "board") : NULL, *column = NULL;
    for (const Sexpr *c = board_node ? sexpr_next_child(node, NULL) : NULL; c && !column; c = sexpr_next_child(node, c)) {
        if (sexpr_is_list(c, "column") && child_list(c, "tabs")) column = c;
    }
    if (!column) {
        render_children(ui, node, ctx);
        return;
    }
    ui->sidebar_column = column;
    ui->sidebar_left = true;   // column before the board?
    for (const Sexpr *c = sexpr_next_child(node, NULL); c != column; c = sexpr_next_child(node, c)) {
        if (c == board_node) ui->sidebar_left = false;
    }
    f32 width = (f32)sexpr_prop_number(column, "width", 400);
    for (const Sexpr *c = sexpr_next_child(node, NULL); c; c = sexpr_next_child(node, c)) {
        if (c == column && !ui->sidebar_left) layout_drag_width_divider(ui, width);   // on the board's side
        render_node(ui, c, ctx);
        if (c == column && ui->sidebar_left) layout_drag_width_divider(ui, width);
    }
}

static void container(Ui *ui, const Sexpr *node, LayoutCtx *ctx, bool horizontal) {
    Clay_Sizing sizing = node_sizing(ui, node, ctx, true);
    u16 gap = prop_px(ui, node, "gap", sexpr_flag(node, "padding") ? ui->theme->shape.gap : 0);
    u16 pad = prop_px(ui, node, "padding", 0);
    LayoutCtx child = *ctx;
    child.horizontal = horizontal;
    if (!sexpr_flag(node, "scroll")) {
        CLAY({ .layout = { .sizing = sizing, .padding = CLAY_PADDING_ALL(pad), .childGap = gap,
                           .layoutDirection = horizontal ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM } }) {
            render_container_children(ui, node, &child, gap);
        }
        ctx->counter = child.counter, ctx->tabs_index = child.tabs_index, ctx->menu_index = child.menu_index;
        return;
    }
    // :scroll (vertical): a viewport around an inner column sized to the viewport
    // but never below :min-height, so growing children fill tall windows and
    // short windows scroll instead of overflowing.
    Clay_ElementId viewport = Clay_GetElementIdWithIndex(CLAY_STRING("ScrollViewport"), ctx->counter++);
    Clay_ElementData last = Clay_GetElementData(viewport);
    f32 available = last.found ? last.boundingBox.height : (f32)GetScreenHeight();
    f32 inner_h = fmaxf(available, ui_px(ui, (f32)sexpr_prop_number(node, "min-height", 640)));
    CLAY({ .id = viewport, .layout = { .sizing = sizing },
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(inner_h) }, .padding = CLAY_PADDING_ALL(pad),
                           .childGap = gap, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            child.horizontal = false;
            render_container_children(ui, node, &child, gap);
        }
    }
    ctx->counter = child.counter, ctx->tabs_index = child.tabs_index, ctx->menu_index = child.menu_index;
}

static void row(Ui *ui, const Sexpr *node, LayoutCtx *ctx) { container(ui, node, ctx, true); }
static void column(Ui *ui, const Sexpr *node, LayoutCtx *ctx) { container(ui, node, ctx, false); }

static void bar(Ui *ui, const Sexpr *node, LayoutCtx *ctx, bool top, bool ghost) {
    const Theme *t = ui->theme;
    Clay_BorderWidth border = { 0 };
    if (top) border.bottom = 1;
    else border.top = 1;
    LayoutCtx child = *ctx;
    child.horizontal = true;
    child.ghost = ghost;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                       .padding = { ui_u16(ui, 12), ui_u16(ui, 12), ui_u16(ui, 8), ui_u16(ui, 8) },
                       .childGap = ui_u16(ui, 6), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(t->colors.surface),
           .border = { .color = color_to_clay(t->colors.border), .width = border } }) {
        render_children(ui, node, &child);
    }
    ctx->counter = child.counter, ctx->tabs_index = child.tabs_index, ctx->menu_index = child.menu_index;
}

static void toolbar(Ui *ui, const Sexpr *node, LayoutCtx *ctx) { bar(ui, node, ctx, true, false); }
static void navbar(Ui *ui, const Sexpr *node, LayoutCtx *ctx) { bar(ui, node, ctx, false, true); }

// ---- Menu bar --------------------------------------------------------------------------------------------

// (menu TITLE-KEY :fallback "File" command command - command ...)
static void menu_dropdown(Ui *ui, const Sexpr *menu, i32 menu_index) {
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    Clay_ElementId panel_id = CLAY_IDI("MenuPanel", (u32)menu_index);
    CLAY({ .id = panel_id,
           .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 150,
                         .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
                         .offset = { 0, ui_px(ui, 2) } },
           .layout = { .sizing = { CLAY_SIZING_FIT(ui_px(ui, 240)), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .padding = CLAY_PADDING_ALL(ui_u16(ui, 5)) },
           .backgroundColor = color_to_clay(color_with_alpha(c->surface, 1.0f)),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius * 0.8f)),
           .border = { .color = color_to_clay(c->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        if (Clay_PointerOver(panel_id)) ui->pointer_over_menu = true;
        i32 k = 0;
        const Sexpr *title = sexpr_next_arg(menu, NULL);
        for (const Sexpr *a = sexpr_next_arg(menu, title); a; a = sexpr_next_arg(menu, a), ++k) {
            StringView id_text = a->text;
            if (sv_eq(id_text, SV("-"))) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = color_to_clay(c->border) }) {}
                continue;
            }
            const CommandDef *cmd = command_find(&app->commands, id_text);
            if (!cmd) continue;
            bool enabled = command_is_enabled(&app->commands, cmd);
            bool checked = command_is_checked(&app->commands, cmd);
            char key[128];
            snprintf(key, sizeof(key), "command.%s", cmd->id);
            char shortcut[48] = "";
            const KeyBinding *binding = keymap_find_command(&app->keymap, id_text);
            if (binding) key_chord_format(binding->chord, shortcut, sizeof(shortcut));
            Clay_ElementId item = CLAY_IDI("MenuItem", (u32)(menu_index * 64 + k));
            bool over = Clay_PointerOver(item) && enabled;
            f32 hover = anim_towards(&ui->anim, item.id, over ? 1.0f : 0.0f, 20.0f, 0.0f);
            Color fg = enabled ? color_lerp(c->text, c->accent_text, hover) : color_with_alpha(c->text_muted, 0.5f);
            CLAY({ .id = item,
                   .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                               .padding = { ui_u16(ui, 6), ui_u16(ui, 10), ui_u16(ui, 5), ui_u16(ui, 5) },
                               .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                   .backgroundColor = color_to_clay(color_with_alpha(c->accent, hover * c->accent.a / 255.0f)),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 16)), CLAY_SIZING_FIXED(ui_px(ui, 16)) } } }) {
                    if (checked) ui_icon(ui, icon_find(SV("check")), 16, fg);
                }
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
                    ui_label(ui, app_tr(app, key, cmd->label), FONT_UI, 14, fg);
                }
                if (shortcut[0]) ui_label(ui, sv(shortcut), FONT_UI, 12, enabled ? color_with_alpha(fg, 0.7f) : fg);
            }
            if (over && ui->mouse_released) {
                ui->open_menu = -1;
                command_execute(&app->commands, id_text);
            }
        }
    }
}

static void menubar(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    UNUSED(ctx);
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .id = CLAY_ID("MenuBar"),
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 2),
                       .padding = { ui_u16(ui, 8), ui_u16(ui, 8), ui_u16(ui, 3), ui_u16(ui, 3) } },
           .backgroundColor = color_to_clay(c->surface) }) {
        bool any_title_hovered = false;
        i32 i = 0;
        for (const Sexpr *menu = sexpr_next_child(node, NULL); menu; menu = sexpr_next_child(node, menu)) {
            if (!sexpr_is_list(menu, "menu")) continue;
            const Sexpr *title = sexpr_next_arg(menu, NULL);
            StringView title_key = title ? title->text : SV("");
            StringView title_text = i18n_get(&ui->app->i18n, title_key, sexpr_prop_text(menu, "fallback", title_key));
            Clay_ElementId id = CLAY_IDI("MenuTitle", (u32)i);
            bool open = ui->open_menu == i, over = Clay_PointerOver(id);
            any_title_hovered |= over;
            f32 t = anim_towards(&ui->anim, id.id, open ? 1.0f : over ? 0.6f : 0.0f, 20.0f, 0.0f);
            CLAY({ .id = id,
                   .layout = { .padding = { ui_u16(ui, 10), ui_u16(ui, 10), ui_u16(ui, 4), ui_u16(ui, 4) } },
                   .backgroundColor = color_to_clay(color_with_alpha(c->hover, t * 2.0f * c->hover.a / 255.0f)),
                   .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
                ui_label(ui, title_text, FONT_UI, 14, c->text);
                if (open) menu_dropdown(ui, menu, i);
            }
            if (over && ui->mouse_pressed) ui->open_menu = open ? -1 : i;
            if (over && ui->open_menu >= 0 && !open) ui->open_menu = i;   // slide between menus
            ++i;
        }
        ui->menu_count = i;
        if (ui->open_menu >= i) ui->open_menu = -1;
        if (ui->open_menu >= 0) ui->pointer_over_menu |= any_title_hovered;
        if (ui->open_menu >= 0 && ui->mouse_pressed && !ui->pointer_over_menu) ui->open_menu = -1;
    }
}

// ---- Registry --------------------------------------------------------------------------------------------

static void app_root(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    CLAY({ .id = CLAY_ID("Root"),
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
        ctx->horizontal = false;
        render_children(ui, node, ctx);
    }
}

static const struct {
    const char *name;
    ComponentFn render;
} COMPONENTS[] = {
    { "app", app_root },          { "row", row },
    { "column", column },         { "menubar", menubar },
    { "toolbar", toolbar },       { "navbar", navbar },
    { "button", button },         { "separator", separator },
    { "spacer", spacer },         { "engine-status", engine_status_widget },
    { "theme-picker", theme_picker }, { "move-counter", move_counter },
    { "status", status_text },    { "board", board },
    { "panel", panel },           { "tabs", tabs },
};

bool layout_component_exists(StringView name) {
    for (i32 i = 0; i < (i32)countof(COMPONENTS); ++i) {
        if (sv_eq(name, sv(COMPONENTS[i].name))) return true;
    }
    return sv_eq(name, SV("menu"));   // only valid inside menubar
}

static void render_node(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    StringView name = sexpr_head(node);
    for (i32 i = 0; i < (i32)countof(COMPONENTS); ++i) {
        if (sv_eq(name, sv(COMPONENTS[i].name))) {
            COMPONENTS[i].render(ui, node, ctx);
            return;
        }
    }
    ui_label(ui, arena_format(&ui->frame, "unknown component: " SV_FMT, SV_ARG(name)), FONT_UI, 12, ui->theme->colors.danger);
}

static void render_children(Ui *ui, const Sexpr *node, LayoutCtx *ctx) {
    for (const Sexpr *c = sexpr_next_child(node, NULL); c; c = sexpr_next_child(node, c)) render_node(ui, c, ctx);
}

// ---- Loading ---------------------------------------------------------------------------------------------

static String layout_path(const Ui *ui) {
    String user = path_join(string_sv(&ui->app->config_dir), SV("layout.lisp"));
    if (os_file_exists(string_sv(&user))) return user;
    string_free(&user);
    return path_join(string_sv(&ui->app->assets_dir), SV("data/layout.lisp"));
}

bool layout_load(Ui *ui) {
    String path = layout_path(ui);
    String text = { 0 };
    bool ok = os_read_file(string_sv(&path), &text);
    Arena arena = { 0 };
    Sexpr *root = NULL;
    SexprError err = { 0 };
    if (ok && !sexpr_parse(&arena, string_sv(&text), &root, &err)) {
        log_error("%s:%d:%d: %s", string_cstr(&path), err.line, err.column, err.message);
        ok = false;
    }
    if (ok && !sexpr_is_list(root->first, "app")) {
        log_error("%s: the layout must be a single (app ...) form", string_cstr(&path));
        ok = false;
    }
    if (ok) {   // swap in the new tree (keep the old one on errors)
        arena_free(&ui->layout_arena);
        ui->layout_arena = arena;
        ui->layout_root = root->first;
        log_info("layout: %s", string_cstr(&path));
    } else {
        arena_free(&arena);
    }
    string_free(&ui->layout_file);
    ui->layout_file = path;
    ui->layout_mtime = os_file_mtime(string_sv(&ui->layout_file));
    string_free(&text);
    return ok;
}

void layout_write_user(Ui *ui, StringView text) {
    String path = path_join(string_sv(&ui->app->config_dir), SV("layout.lisp"));
    os_make_dirs(string_sv(&ui->app->config_dir));
    if (!os_write_file(string_sv(&path), text)) app_set_status(ui->app, "Could not write %s", string_cstr(&path));
    string_free(&path);
    layout_load(ui);
}

void layout_free(Ui *ui) {
    arena_free(&ui->layout_arena);
    ui->layout_root = NULL;
    string_free(&ui->layout_file);
}

void layout_build(Ui *ui) {
    ui->pointer_over_menu = false;
    // Hot reload: re-read the layout when its file changes (checked twice a second).
    if (os_time_seconds() - ui->layout_checked > 0.5) {
        ui->layout_checked = os_time_seconds();
        if (os_file_mtime(string_sv(&ui->layout_file)) != ui->layout_mtime) layout_load(ui);
    }
    layout_drag_update(ui);   // uses last frame's sidebar
    ui->sidebar_column = NULL;
    ui->sidebar_stack_count = ui->sidebar_tab_count = 0;
    ui->panel_stack_index = -1;
    LayoutCtx ctx = { 0 };
    if (ui->layout_root) render_node(ui, ui->layout_root, &ctx);
    layout_drag_overlay(ui);
    ui_dialogs(ui);
}

// Visits every node (depth-first) — used for validation.
void layout_visit(const Sexpr *node, void (*visit)(const Sexpr *node, void *user), void *user) {
    if (!node) return;
    visit(node, user);
    for (const Sexpr *c = sexpr_next_child(node, NULL); c; c = sexpr_next_child(node, c)) layout_visit(c, visit, user);
}
