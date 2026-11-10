// Direct manipulation of the sidebar in the live window:
//   - drag a panel by its title (or a tab by its button) to another place in
//     the panel stack or the tab group;
//   - drag the divider between the board and the sidebar to set its width;
//   - drag the handle under a panel that has a :height to resize it.
// Panel drags start after a few pixels of movement, so clicks stay clicks.
// Hit testing uses last frame's layout (Clay element data). A drop or release
// goes through LayoutModel and is saved as the user's layout.lisp, exactly
// like the Customize layout dialog.
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>

enum { DRAG_THRESHOLD = 6, TITLE_STRIP = 34, MIN_WIDTH = 260, MAX_WIDTH = 900, MIN_HEIGHT = 60, MAX_HEIGHT = 600 };

static Clay_BoundingBox box_of(Clay_ElementId id, bool *found) {
    Clay_ElementData d = Clay_GetElementData(id);
    if (found) *found = d.found;
    return d.boundingBox;
}

static bool in_box(Vector2 p, Clay_BoundingBox b) { return p.x >= b.x && p.x < b.x + b.width && p.y >= b.y && p.y < b.y + b.height; }

static bool can_start(const Ui *ui) {
    return ui->layout_drag.kind == LAYOUT_DRAG_NONE && ui->dialog == UI_DIALOG_NONE && !ui->pointer_over_menu && ui->open_menu < 0 &&
           ui->active_id == 0;
}

static StringView panel_title(Ui *ui, StringView name) {
    char key[96];
    snprintf(key, sizeof(key), "panel." SV_FMT, SV_ARG(name));
    return app_tr(ui->app, key, key + 6);
}

static void begin(Ui *ui, LayoutDragKind kind, LayoutSlot from, f32 value) {
    LayoutDrag *d = &ui->layout_drag;
    *d = (LayoutDrag){ .kind = kind, .from = from, .press = ui->mouse, .start_value = value, .value = value };
    d->active = kind != LAYOUT_DRAG_PANEL;   // dividers follow the mouse at once
}

// ---- Hooks called while the layout is built -------------------------------------------------------

void layout_drag_stack_item(Ui *ui, Clay_ElementId id, isize index, const Sexpr *item) {
    if (!ui->mouse_pressed || !can_start(ui) || !sexpr_is_list(item, "panel")) return;
    bool found;
    Clay_BoundingBox b = box_of(id, &found);
    if (!found || !in_box(ui->mouse, b) || ui->mouse.y > b.y + ui_px(ui, TITLE_STRIP)) return;
    const Sexpr *name = sexpr_next_arg(item, NULL);
    begin(ui, LAYOUT_DRAG_PANEL, (LayoutSlot){ false, index }, 0);
    snprintf(ui->layout_drag.label, sizeof(ui->layout_drag.label), SV_FMT, SV_ARG(panel_title(ui, name ? name->text : SV(""))));
}

void layout_drag_tab(Ui *ui, i32 index, StringView label) {
    if (!can_start(ui)) return;
    begin(ui, LAYOUT_DRAG_PANEL, (LayoutSlot){ true, index }, 0);
    snprintf(ui->layout_drag.label, sizeof(ui->layout_drag.label), SV_FMT, SV_ARG(label));
}

f32 layout_drag_width(const Ui *ui, f32 width) {
    const LayoutDrag *d = &ui->layout_drag;
    return d->kind == LAYOUT_DRAG_WIDTH ? d->value : width;
}

f32 ui_panel_height(const Ui *ui, f32 fallback) {
    const LayoutDrag *d = &ui->layout_drag;
    if (d->kind == LAYOUT_DRAG_HEIGHT && ui->panel_stack_index == d->from.index) return d->value;
    return (f32)sexpr_prop_number(ui->panel_node, "height", fallback);
}

// A thin hit area; the line shows on hover and while dragging.
static void handle_line(Ui *ui, Clay_ElementId id, bool vertical, bool dragging) {
    const ThemeColors *c = &ui->theme->colors;
    bool over = Clay_PointerOver(id) && ui->layout_drag.kind == LAYOUT_DRAG_NONE;   // not while another drag passes over
    f32 t = anim_towards(&ui->anim, id.id, over || dragging ? 1.0f : 0.0f, 14.0f, 0.0f);
    Clay_Sizing line = vertical ? (Clay_Sizing){ CLAY_SIZING_FIXED(ui_px(ui, 2)), CLAY_SIZING_PERCENT(0.6f) }
                                : (Clay_Sizing){ CLAY_SIZING_PERCENT(0.3f), CLAY_SIZING_FIXED(ui_px(ui, 3)) };
    CLAY({ .layout = { .sizing = line }, .backgroundColor = color_to_clay(color_with_alpha(c->accent, t)),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 2)) }) {}
    if (over || dragging) SetMouseCursor(vertical ? MOUSE_CURSOR_RESIZE_EW : MOUSE_CURSOR_RESIZE_NS);
}

void layout_drag_width_divider(Ui *ui, f32 width) {
    Clay_ElementId id = CLAY_ID("SidebarDivider");
    bool dragging = ui->layout_drag.kind == LAYOUT_DRAG_WIDTH;
    CLAY({ .id = id, .layout = { .sizing = { CLAY_SIZING_FIXED(ui_px(ui, 6)), CLAY_SIZING_GROW(0) },
                                 .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
        handle_line(ui, id, true, dragging);
    }
    if (ui->mouse_pressed && can_start(ui) && Clay_PointerOver(id)) begin(ui, LAYOUT_DRAG_WIDTH, (LayoutSlot){ 0 }, width);
}

// Floats over the gap under the stack item (no effect on the layout).
void layout_drag_height_handle(Ui *ui, isize index, f32 height, f32 gap) {
    Clay_ElementId parent = CLAY_IDI("SidebarItem", (u32)index), id = CLAY_IDI("SidebarResize", (u32)index);
    Clay_BoundingBox b = box_of(parent, NULL);
    f32 h = fmaxf(gap, ui_px(ui, 8));
    bool dragging = ui->layout_drag.kind == LAYOUT_DRAG_HEIGHT && ui->layout_drag.from.index == index;
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_FIXED(fmaxf(b.width, 1.0f)), CLAY_SIZING_FIXED(h) },
                       .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
           .floating = { .attachTo = CLAY_ATTACH_TO_PARENT,
                         .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM },
                         .offset = { 0, (gap - h) * 0.5f },
                         .zIndex = 5,
                         .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT } }) {
        handle_line(ui, id, false, dragging);
    }
    if (ui->mouse_pressed && can_start(ui) && Clay_PointerOver(id)) begin(ui, LAYOUT_DRAG_HEIGHT, (LayoutSlot){ false, index }, height);
}

// ---- Drop targets (last frame's layout) ------------------------------------------------------------

static bool find_target(Ui *ui, LayoutSlot *out) {
    Vector2 m = ui->mouse;
    bool bar_found;
    Clay_BoundingBox bar = box_of(CLAY_IDI("SidebarTabBar", (u32)ui->sidebar_tabs_slot), &bar_found);
    f32 slack = ui_px(ui, 12);
    if (bar_found && ui->sidebar_tab_count > 0 && m.x >= bar.x - slack && m.x < bar.x + bar.width + slack && m.y >= bar.y - slack &&
        m.y < bar.y + bar.height + slack) {
        isize index = 0;
        for (i32 t = 0; t < ui->sidebar_tab_count; ++t) {
            Clay_BoundingBox tb = box_of(CLAY_IDI("SidebarTab", (u32)(ui->sidebar_tabs_slot * 32 + t)), NULL);
            if (m.x > tb.x + tb.width * 0.5f) index = t + 1;
        }
        *out = (LayoutSlot){ true, index };
        return true;
    }
    if (ui->sidebar_stack_count <= 0) return false;
    Clay_BoundingBox first = box_of(CLAY_IDI("SidebarItem", 0), NULL);
    f32 bottom = bar_found ? bar.y : INFINITY;
    if (m.x < first.x - slack || m.x >= first.x + first.width + slack || m.y >= bottom) return false;
    isize index = 0;
    for (isize i = 0; i < ui->sidebar_stack_count; ++i) {
        Clay_BoundingBox ib = box_of(CLAY_IDI("SidebarItem", (u32)i), NULL);
        if (m.y > ib.y + ib.height * 0.5f) index = i + 1;
    }
    *out = (LayoutSlot){ false, index };
    return true;
}

static void update(Ui *ui, LayoutDrag *d);

// ---- Commit -----------------------------------------------------------------------------------------

static void commit(Ui *ui) {
    LayoutDrag *d = &ui->layout_drag;
    String text = { 0 };
    if (!os_read_file(string_sv(&ui->layout_file), &text)) return;
    LayoutModel m = { 0 };
    const char *error = NULL;
    bool changed = false;
    if (!layout_model_read(string_sv(&text), &m, &error)) {
        app_set_status(ui->app, "Layout: %s", error);
    } else if (d->kind == LAYOUT_DRAG_PANEL && d->has_target) {
        changed = layout_model_move_panel(&m, d->from, d->target);
        if (changed && d->target.tabs) {   // show the panel that was just dropped into the tabs
            isize at = !d->from.tabs || d->target.index <= d->from.index ? d->target.index : d->target.index - 1;
            ui->tab_selected[ui->sidebar_tabs_slot] = (i32)at;
        }
    } else if (d->kind == LAYOUT_DRAG_WIDTH) {
        i32 w = (i32)lroundf(d->value);
        changed = w != m.sidebar_width;
        m.sidebar_width = w;
    } else if (d->kind == LAYOUT_DRAG_HEIGHT && d->from.index < m.stack.len) {
        String *item = &m.stack.items[d->from.index];
        String updated = layout_item_with_number(string_sv(item), SV("height"), (i32)lroundf(d->value));
        changed = !sv_eq(string_sv(&updated), string_sv(item));
        string_free(item);
        *item = updated;
    }
    if (changed) {
        String out = layout_model_write(string_sv(&text), &m);
        layout_write_user(ui, string_sv(&out));
        string_free(&out);
    }
    layout_model_free(&m);
    string_free(&text);
}

// Before the layout: thresholds, values, cursor, drop.
void layout_drag_update(Ui *ui) {
    LayoutDrag *d = &ui->layout_drag;
    if (d->kind == LAYOUT_DRAG_NONE) return;
    if (ui->dialog != UI_DIALOG_NONE) {
        *d = (LayoutDrag){ 0 };
        return;
    }
    update(ui, d);
    // The drag owns the mouse: the graph, the board and buttons it passes over
    // must not see the button held down (nor its release).
    ui->mouse_pressed = ui->mouse_down = ui->mouse_released = false;
}

static void update(Ui *ui, LayoutDrag *d) {
    f32 dx = (ui->mouse.x - d->press.x) / ui->scale, dy = (ui->mouse.y - d->press.y) / ui->scale;
    switch (d->kind) {
        case LAYOUT_DRAG_PANEL:
            if (!d->active && hypotf(dx, dy) * ui->scale >= DRAG_THRESHOLD) d->active = true;
            if (d->active) {
                d->has_target = find_target(ui, &d->target);
                SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
            }
            break;
        case LAYOUT_DRAG_WIDTH:
            d->value = CLAMP(d->start_value + (ui->sidebar_left ? dx : -dx), MIN_WIDTH, MAX_WIDTH);
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
            break;
        case LAYOUT_DRAG_HEIGHT:
            d->value = CLAMP(d->start_value + dy, MIN_HEIGHT, MAX_HEIGHT);
            SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
            break;
        default: break;
    }
    if (!ui->mouse_down) {   // released (also catches a release outside the window)
        if (d->active) commit(ui);
        *d = (LayoutDrag){ 0 };
    }
}

// After the layout: the dragged panel's label at the pointer and where it would land.
void layout_drag_overlay(Ui *ui) {
    const LayoutDrag *d = &ui->layout_drag;
    if (d->kind != LAYOUT_DRAG_PANEL || !d->active) return;
    const ThemeColors *c = &ui->theme->colors;
    if (d->has_target) {
        Clay_BoundingBox r;
        f32 t = ui_px(ui, 3);
        if (d->target.tabs) {
            Clay_BoundingBox bar = box_of(CLAY_IDI("SidebarTabBar", (u32)ui->sidebar_tabs_slot), NULL);
            f32 x = bar.x + ui_px(ui, 2);
            if (d->target.index > 0) {
                Clay_BoundingBox tb = box_of(CLAY_IDI("SidebarTab", (u32)(ui->sidebar_tabs_slot * 32 + d->target.index - 1)), NULL);
                x = tb.x + tb.width + ui_px(ui, 1);
            }
            r = (Clay_BoundingBox){ x - t * 0.5f, bar.y + ui_px(ui, 3), t, bar.height - ui_px(ui, 6) };
        } else {
            isize n = ui->sidebar_stack_count;
            Clay_BoundingBox last = box_of(CLAY_IDI("SidebarItem", (u32)(n - 1)), NULL);
            Clay_BoundingBox at = d->target.index < n ? box_of(CLAY_IDI("SidebarItem", (u32)d->target.index), NULL) : last;
            f32 y = d->target.index < n ? at.y - ui->sidebar_gap * 0.5f : last.y + last.height + ui->sidebar_gap * 0.5f;
            r = (Clay_BoundingBox){ at.x, y - t * 0.5f, at.width, t };
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(r.width), CLAY_SIZING_FIXED(r.height) } },
               .backgroundColor = color_to_clay(c->accent),
               .cornerRadius = CLAY_CORNER_RADIUS(t * 0.5f),
               .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { r.x, r.y }, .zIndex = 150 } }) {}
    }
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0) },
                       .padding = { ui_u16(ui, 10), ui_u16(ui, 10), ui_u16(ui, 5), ui_u16(ui, 5) } },
           .backgroundColor = color_to_clay(color_with_alpha(c->surface, 0.92f)),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, ui->theme->shape.radius)),
           .border = { .color = color_to_clay(c->accent), .width = CLAY_BORDER_OUTSIDE(1) },
           .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { ui->mouse.x + ui_px(ui, 14), ui->mouse.y + ui_px(ui, 10) },
                         .zIndex = 151, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
        CLAY_TEXT(ui_str(ui, sv(d->label)), ui_text_style(ui, FONT_UI_BOLD, 13, d->has_target ? c->text : c->text_muted));
    }
}
