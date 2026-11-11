// ui.h - the UI layer: owns window-side resources (fonts, themes, textures),
// builds the Clay layout from data (assets/data/layout.lisp) every frame and
// draws it. Talks to the App only via commands and app_* functions.
#pragma once

#include "app/app.h"
#include "app/installer.h"
#include "base/sexpr.h"
#include "ui/anim.h"
#include "ui/background.h"
#include "ui/layout_model.h"
#include "ui/clay_render.h"
#include "ui/font_cache.h"
#include "ui/theme.h"

// ---- Board geometry -----------------------------------------------------------------------

typedef struct BoardGeometry {
    Rectangle bounds;   // custom element rectangle
    Rectangle board;    // board surface
    Vector2 origin;     // centre of point (0,0)
    f32 cell;
    i32 width, height;
    bool valid;
} BoardGeometry;

// ---- Text editing ----------------------------------------------------------------------------

#include "ui/text_edit.h"
#include "ui/stone_anim.h"   // TextEdit (editing model, no raylib)



// ---- Ui --------------------------------------------------------------------------------------


typedef enum UiDialog {
    UI_DIALOG_NONE,
    UI_DIALOG_SETTINGS,
    UI_DIALOG_NEW_GAME,
    UI_DIALOG_SETUP,
    UI_DIALOG_GAME_INFO,
    UI_DIALOG_REPORT,
    UI_DIALOG_THEME_EDITOR,
    UI_DIALOG_LAYOUT_EDITOR,
    UI_DIALOG_UPDATE,
} UiDialog;

// Theme editor state (theme_editor.c): `edit` is the live copy ui->theme points to while open.
typedef struct ThemeEditor {
    bool open;
    Theme edit;
    isize base_index;       // theme it started from (Cancel / Reset)
    i32 group, selected;    // colour group tab, selected colour
    i32 drag;               // slider being dragged (channel + 1), 0 = none
    f32 hsv[4];             // hue 0..360, saturation, value, alpha (selected colour)
    TextEdit name, hex;
    u32 version;            // bumped on every edit (board layer cache key)
    bool background_stale;  // background colour changed: rebuild the paper when the drag ends
} ThemeEditor;

// "Customize layout" dialog state (layout_editor.c).
typedef struct LayoutEditor {
    bool open;
    LayoutModel model;
    String source;          // text the model's positions refer to (the active layout file)
    bool had_user_file;     // <config_dir>/layout.lisp existed when the dialog opened
    String original_user;   // ...and its contents (Undo changes)
    TextEdit preset_name;   // "Save as preset"
} LayoutEditor;

// Sidebar drag in the live window (layout_drag.c).
typedef enum LayoutDragKind { LAYOUT_DRAG_NONE, LAYOUT_DRAG_PANEL, LAYOUT_DRAG_WIDTH, LAYOUT_DRAG_HEIGHT } LayoutDragKind;
typedef struct LayoutDrag {
    LayoutDragKind kind;
    bool active;              // panel drags: moved past the threshold
    LayoutSlot from;          // panel being moved / stack item being resized
    Vector2 press;
    f32 start_value, value;   // width / height in layout units
    LayoutSlot target;        // drop gap
    bool has_target;
    char label[64];
} LayoutDrag;

// Markup mode tools (markup_tools.c).
typedef enum MarkTool {
    MARK_TOOL_BLACK, MARK_TOOL_WHITE, MARK_TOOL_TRIANGLE, MARK_TOOL_SQUARE, MARK_TOOL_CIRCLE, MARK_TOOL_CROSS,
    MARK_TOOL_LETTER, MARK_TOOL_NUMBER, MARK_TOOL_ARROW, MARK_TOOL_LINE, MARK_TOOL_PEN, MARK_TOOL_SHADE, MARK_TOOL_ERASER,
    MARK_TOOL_PLAY,   // plays moves (alternating colours, captures) like the board in Analysis mode
    MARK_TOOLS
} MarkTool;
typedef struct MarkupDrag {
    bool active, erase, rect;
    Point start, last;
    f32 pen[256][2];      // board coordinates
    i32 pen_count;
    f64 last_click;       // double click (shade: fill the empty region)
    Point last_click_point;
} MarkupDrag;

typedef struct Ui {
    App *app;
    FontCache *fonts;
    ThemeVec themes;
    isize theme_index;
    const Theme *theme;
    Background background;
    Texture2D board_texture;
    String board_texture_for;
    // Layout tree (assets/data/layout.lisp or <config_dir>/layout.lisp).
    Arena layout_arena;
    const Sexpr *layout_root;
    String layout_file;
    i64 layout_mtime;
    f64 layout_checked;
    const Sexpr *panel_node;   // props of the (panel ...) being rendered
    i32 menu_count;

    Arena frame;          // reset every frame (Clay strings, custom element payloads)
    void *clay_memory;
    f32 scale;
    bool debug;
    char help_topic[32];   // open "?" popover (empty = none)
    i32 deep_report_tab;
    i32 deep_report_filter;   // All moves tab: bit 0 Black, bit 1 White, bit 2 losses >= 2 points
    ThemeEditor theme_editor;
    LayoutEditor layout_editor;
    i32 form_tab;          // selected section of a tabbed form (Settings)
    bool help_button_hovered;   // a "?" button is under the mouse (this frame)

    // Input for this frame.
    Vector2 mouse;
    bool mouse_pressed, mouse_released, mouse_down;
    f32 wheel;
    u32 active_id;        // Clay id of the button being pressed
    u32 focus_id;         // Clay id of the focused text input (0 = none)
    bool focus_claimed;   // a focused input was laid out this frame
    bool text_dragging;
    u32 scrollbar_drag;   // scrollbar thumb being dragged (Clay id), 0 = none
    f32 scrollbar_grab;   // mouse y minus the thumb's offset when it was grabbed
    bool theme_menu_open;
    i32 open_menu;        // index into layout.menus, -1 = closed
    bool pointer_over_menu;
    // Modal dialogs, built from assets/data/forms.lisp.
    UiDialog dialog;
    Arena forms_arena;
    const Sexpr *forms_root;   // assets/data/forms.lisp
    TextEdit form_edits[16];                    // text fields, keyed by config field
    const struct ConfigField *form_edit_fields[16];
    i32 form_edit_count;
    u32 language_version;

    // Win-rate bar animation (holds the last value while new analysis arrives).
    bool eval_known;
    f32 eval_black_winrate, eval_black_score;   // displayed (animated) values

    // Engine setup (downloads).
    Installer installer;
    bool setup_init, setup_reloaded;
    bool setup_engine, setup_network, setup_human;

    // Comment editor state.
    TextEdit comment;
    GameNode *comment_node;
    u32 comment_generation;   // app->tree_generation when comment_node was taken

    // Variation tree.
    GameNode *tree_scrolled_to;
    GameNode *move_list_scrolled_to;
    i32 tab_selected[16];   // per tab group (sidebar entry index)
    bool in_tab;            // panels inside a tab group skip their card title

    // Toolbar modes (layout.lisp: (modes (mode analysis ...) (mode markup ...))).
    char toolbar_mode[24];           // active mode name ("" = the first)
    char toolbar_prev[24];           // the one sliding out
    f64 toolbar_switched;            // when (GetTime)
    const Sexpr *toolbar_modes;      // the (modes ...) node this frame
    bool markup_mode;                // the active mode is "markup": clicks mark the board
    MarkTool markup_tool;
    i32 markup_color;                // palette index (config markup.colors)
    bool markup_color_editing;       // the colour editor popover is open
    i32 markup_slider;               // H/S/V slider being dragged (+1)
    MarkupDrag markup_drag;

    // The sidebar as laid out this frame (for drag and drop).
    LayoutDrag layout_drag;
    const Sexpr *sidebar_column;   // (column ... (tabs ...)) next to the board
    bool sidebar_left;
    bool in_sidebar_tabs;          // rendering the sidebar's tab group
    i32 sidebar_tabs_slot, sidebar_tab_count;
    isize sidebar_stack_count;
    f32 sidebar_gap;
    isize panel_stack_index;       // stack index of the panel being rendered (-1 = not in the stack)

    AnimStore anim;
    // Smooth wheel scrolling: the hovered scroll container glides to a target.
    u32 scroll_id;          // Clay id of the container being animated
    f32 scroll_target;      // desired scrollPosition.y (<= 0)
    f32 scroll_last;        // position we set last frame (detects drags / clamps)

    // Board rendering: the static layer (surface, grid, coordinates) is cached
    // in a render texture and rebuilt only when its inputs change.
    RenderTexture2D board_layer;
    u64 board_layer_key;
    StoneAnim stone_anim;   // stones easing in / fading out (stone_anim.c)
    u64 candidates_position;

    // Board interaction (computed from last layout).
    BoardGeometry board_geo;
    Point hover_point;
    i32 hover_candidate;  // index into app->analysis or -1
    i32 hover_list_candidate;   // hovered in the candidates panel
    // Region (tsumego) selection: the next drag on the board defines it.
    bool region_select;
    bool region_dragging;
    Point region_start, region_end;
    GameNode *graph_hover;
} Ui;

bool ui_init(Ui *ui, App *app);
void ui_shutdown(Ui *ui);
void ui_frame(Ui *ui);
void ui_apply_theme(Ui *ui, isize index);
void ui_reload_themes(Ui *ui);   // built-in + user themes, again (after saving one)
void theme_editor_open(Ui *ui);
void theme_editor_cancel(Ui *ui);
void theme_editor_dialog(Ui *ui);
void layout_editor_open(Ui *ui);
void layout_editor_close(Ui *ui);
void layout_editor_dialog(Ui *ui);

// ---- Shared helpers (widgets.c) ---------------------------------------------------------------

static inline f32 ui_px(const Ui *ui, f32 v) { return v * ui->scale; }
static inline u16 ui_u16(const Ui *ui, f32 v) { return (u16)(v * ui->scale + 0.5f); }
Clay_String ui_str(Ui *ui, StringView s);
Clay_String ui_fmt(Ui *ui, const char *fmt, ...) PRINTF_LIKE(2, 3);
CustomElement *ui_custom(Ui *ui, CustomDrawFn draw, void *user);

Clay_TextElementConfig *ui_text_style(Ui *ui, FontRole role, f32 size, Color color);
void ui_label(Ui *ui, StringView text, FontRole role, f32 size, Color color);
void ui_stone_dot(Ui *ui, Stone color);   // small stone marker (panels.c)
void deep_report_dialog(Ui *ui);          // deep_report.c
// Inserts line breaks so text fits max_width: breaks at spaces for Latin
// words and between any two characters of CJK text. Result lives in the frame arena.
StringView ui_wrap_text(Ui *ui, StringView text, FontRole role, f32 size, f32 max_width);

// Vector icons (icons.c). Buttons accept "@name optional text" labels.
i32 icon_find(StringView name);   // "@prev" or "prev"; -1 if unknown
void icon_draw(i32 icon, Rectangle r, Color color, f32 stroke);
void ui_icon(Ui *ui, i32 icon, f32 size, Color color);

typedef enum ButtonStyle { BUTTON_GHOST, BUTTON_NORMAL, BUTTON_PRIMARY } ButtonStyle;
// Immediate-mode button; returns true when clicked this frame.
bool ui_button(Ui *ui, Clay_ElementId id, StringView label, ButtonStyle style, bool checked, bool enabled);
// Text input (single or multi-line). The caller owns `edit`.
TextInputResult ui_text_input(Ui *ui, Clay_ElementId id, TextEdit *edit, f32 font_size, i32 min_rows, StringView placeholder);
// Small on/off switch; returns true when toggled this frame.
bool ui_toggle(Ui *ui, Clay_ElementId id, bool on);
// Button bound to a registered command (label from i18n "command.<id>").
void ui_command_button(Ui *ui, StringView command_id, StringView icon, ButtonStyle style, u32 index);
void ui_command_button_ex(Ui *ui, StringView command_id, StringView icon, bool with_label, ButtonStyle style, u32 index);
Clay_ElementDeclaration ui_card(Ui *ui, Clay_ElementId id);
// Vertically scrolling area. Inside a tab group it grows to fill the (fixed
// size) tab box; elsewhere it is `height` px tall (or fits its content when
// height <= 0). Must be evaluated inside CLAY(...) so the scroll offset is
// read for the element being opened.
Clay_ElementDeclaration ui_scroll_area(Ui *ui, Clay_ElementId id, f32 height);
// A thin scrollbar on the right of scroll container `id` (call inside it), shown
// when its content is taller than it; the thumb can be dragged. `z_index`: the
// container's layer (floating z-order is absolute: a dialog's content needs > 200).
void ui_scrollbar(Ui *ui, Clay_ElementId id, i16 z_index);
void ui_card_title(Ui *ui, StringView title);
// Card title with a "?" button at the right that explains the panel
// (l10n keys help.<topic>.title / help.<topic>.text); shown in tabs too.
void ui_card_title_help(Ui *ui, StringView title, StringView topic);
void ui_help_button(Ui *ui, StringView topic);

// ---- Pieces (board_view.c, winrate_graph.c, panels.c, layout.c) ----------------------------------

void board_view_element(Ui *ui);
void board_view_handle_input(Ui *ui);
// Before BeginDrawing: refresh the cached board layer and stone animations.
void board_view_prepare(Ui *ui);
void board_view_free(Ui *ui);
void winrate_graph_element(Ui *ui, f32 height);
void variation_tree_element(Ui *ui, f32 height);

// ---- Forms & dialogs (form.c) -------------------------------------------------------------------
typedef void (*ModalSection)(Ui *ui, void *user);
const Sexpr *ui_form(Ui *ui, const char *name);   // (form NAME ...) from forms.lisp
void form_render(Ui *ui, const Sexpr *form);
// Modal with title, scrolling form body and an optional footer (buttons).
void ui_modal(Ui *ui, const Sexpr *form, f32 width, ModalSection footer, void *user);
// Modal with any body (same header, close button, backdrop and animation).
void ui_modal_shell(Ui *ui, StringView title, f32 width, ModalSection body, ModalSection footer, void *user);
void engine_setup_dialog(Ui *ui);   // setup.c
void engine_setup_poll(Ui *ui);     // reload engines once an install finishes
void update_dialog(Ui *ui);         // update_dialog.c
void update_dialog_poll(Ui *ui);    // opens it when the app asks
void ui_dialogs(Ui *ui);                           // renders the open dialog, if any
void ui_close_dialog(Ui *ui);
void panels_render(Ui *ui, StringView name);
bool layout_load(Ui *ui);    // (re)reads the layout file; keeps the old tree on errors
void layout_free(Ui *ui);
void layout_build(Ui *ui);   // hot-reloads, renders the tree and open dialogs
void layout_write_user(Ui *ui, StringView text);   // saves <config_dir>/layout.lisp and reloads it
void toolbar_switch(Ui *ui, StringView mode);      // slide to that toolbar mode
void toolbar_next(Ui *ui);
void markup_tools_register(Ui *ui);
void markup_tools_input(Ui *ui, const BoardGeometry *g);     // instead of playing moves, in Markup mode
void markup_tools_preview(Ui *ui, const BoardGeometry *g);   // the drag in progress
void markup_colors_element(Ui *ui);                          // (markup-colors): shading / pen palette
void markup_counts_element(Ui *ui);                          // (markup-counts): shaded points per colour

// layout_drag.c: drag panels / tabs and the sidebar dividers in the live window.
void layout_drag_update(Ui *ui);    // before the layout
void layout_drag_overlay(Ui *ui);   // after it (insertion marker, dragged label)
void layout_drag_stack_item(Ui *ui, Clay_ElementId id, isize index, const Sexpr *item);
void layout_drag_tab(Ui *ui, i32 index, StringView label);
void layout_drag_width_divider(Ui *ui, f32 width);
void layout_drag_height_handle(Ui *ui, isize index, f32 height, f32 gap);
f32 layout_drag_width(const Ui *ui, f32 width);
f32 ui_panel_height(const Ui *ui, f32 fallback);   // the panel's :height (live while being resized)
bool layout_component_exists(StringView name);
void layout_visit(const Sexpr *node, void (*visit)(const Sexpr *node, void *user), void *user);
