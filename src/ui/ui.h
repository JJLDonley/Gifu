// ui.h - the UI layer: owns window-side resources (fonts, themes, textures),
// builds the Clay layout from data (assets/data/layout.json) every frame and
// draws it. Talks to the App only via commands and app_* functions.
#pragma once

#include "app/app.h"
#include "ui/anim.h"
#include "ui/background.h"
#include "ui/clay_render.h"
#include "ui/font_cache.h"
#include "ui/theme.h"

// ---- Layout description (parsed from layout.json) ------------------------------------

typedef enum LayoutItemKind {
    LAYOUT_ITEM_COMMAND,
    LAYOUT_ITEM_SEPARATOR,
    LAYOUT_ITEM_SPACER,
    LAYOUT_ITEM_ENGINE_STATUS,
    LAYOUT_ITEM_THEME_PICKER,
    LAYOUT_ITEM_MOVE_COUNTER,
} LayoutItemKind;

typedef struct LayoutItem {
    LayoutItemKind kind;
    StringView command;
    StringView icon;
    bool primary;
    bool with_label;   // icon + translated command label
} LayoutItem;

typedef Vec(LayoutItem) LayoutItemVec;
typedef Vec(StringView) StringViewVec;

// A sidebar entry is a single panel, or a tab group showing one of several.
typedef struct SidebarEntry {
    StringView panel;   // empty for tab groups
    i32 tab_start;      // index into LayoutSpec.tab_panels
    i32 tab_count;
} SidebarEntry;
typedef Vec(SidebarEntry) SidebarEntryVec;

typedef struct LayoutSpec {
    LayoutItemVec toolbar;
    LayoutItemVec navbar;
    SidebarEntryVec panels;
    StringViewVec tab_panels;
    f32 sidebar_width;
    bool sidebar_left;
} LayoutSpec;

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

typedef struct TextEdit {
    String text;
    isize caret, anchor;   // byte offsets; anchor == caret means no selection
    bool multiline;
    f64 blink_start;
} TextEdit;

typedef enum TextInputResult {
    TEXT_INPUT_NONE,
    TEXT_INPUT_CHANGED,
    TEXT_INPUT_COMMITTED,   // focus left (click outside, Enter on single-line, Ctrl+Enter)
    TEXT_INPUT_CANCELLED,   // Escape
} TextInputResult;

void text_edit_set(TextEdit *edit, StringView text);
void text_edit_free(TextEdit *edit);

// ---- Ui --------------------------------------------------------------------------------------

typedef struct StoneGhost {   // a removed stone fading out
    Point point;
    Stone color;
    f64 born;
} StoneGhost;

typedef enum UiDialog {
    UI_DIALOG_NONE,
    UI_DIALOG_SETTINGS,
    UI_DIALOG_NEW_GAME,
} UiDialog;

typedef struct Ui {
    App *app;
    FontCache *fonts;
    ThemeVec themes;
    isize theme_index;
    const Theme *theme;
    Background background;
    Texture2D board_texture;
    String board_texture_for;
    LayoutSpec layout;

    Arena frame;          // reset every frame (Clay strings, custom element payloads)
    void *clay_memory;
    f32 scale;
    bool debug;

    // Input for this frame.
    Vector2 mouse;
    bool mouse_pressed, mouse_released, mouse_down;
    f32 wheel;
    u32 active_id;        // Clay id of the button being pressed
    u32 focus_id;         // Clay id of the focused text input (0 = none)
    bool focus_claimed;   // a focused input was laid out this frame
    bool text_dragging;
    bool theme_menu_open;
    bool pointer_over_menu;
    // Modal dialogs, built from assets/data/forms.json.
    UiDialog dialog;
    JsonDoc forms_doc;
    TextEdit form_edits[8];                     // text fields, keyed by config field
    const struct ConfigField *form_edit_fields[8];
    i32 form_edit_count;
    u32 language_version;

    // Win-rate bar animation (holds the last value while new analysis arrives).
    bool eval_known;
    f32 eval_black_winrate, eval_black_score;   // displayed (animated) values

    // Comment editor state.
    TextEdit comment;
    GameNode *comment_node;

    // Variation tree.
    GameNode *tree_scrolled_to;
    GameNode *move_list_scrolled_to;
    i32 tab_selected[16];   // per tab group (sidebar entry index)
    bool in_tab;            // panels inside a tab group skip their card title

    AnimStore anim;
    f32 scroll_pending;     // wheel distance not yet applied (smooth scrolling)

    // Board rendering: the static layer (surface, grid, coordinates) is cached
    // in a render texture and rebuilt only when its inputs change.
    RenderTexture2D board_layer;
    u64 board_layer_key;
    // Stone animations: board seen last frame, per-point appear time, and
    // fading "ghosts" of removed stones.
    u8 prev_cells[BOARD_MAX_POINTS];
    i32 prev_width, prev_height;
    f64 stone_born[BOARD_MAX_POINTS];
    StoneGhost ghosts[64];
    i32 ghost_count;
    u64 candidates_position;

    // Board interaction (computed from last layout).
    BoardGeometry board_geo;
    Point hover_point;
    i32 hover_candidate;  // index into app->analysis or -1
    i32 hover_list_candidate;   // hovered in the candidates panel
    GameNode *graph_hover;
} Ui;

bool ui_init(Ui *ui, App *app);
void ui_shutdown(Ui *ui);
void ui_frame(Ui *ui);
void ui_apply_theme(Ui *ui, isize index);

// ---- Shared helpers (widgets.c) ---------------------------------------------------------------

static inline f32 ui_px(const Ui *ui, f32 v) { return v * ui->scale; }
static inline u16 ui_u16(const Ui *ui, f32 v) { return (u16)(v * ui->scale + 0.5f); }
Clay_String ui_str(Ui *ui, StringView s);
Clay_String ui_fmt(Ui *ui, const char *fmt, ...) PRINTF_LIKE(2, 3);
CustomElement *ui_custom(Ui *ui, CustomDrawFn draw, void *user);

Clay_TextElementConfig *ui_text_style(Ui *ui, FontRole role, f32 size, Color color);
void ui_label(Ui *ui, StringView text, FontRole role, f32 size, Color color);
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
void ui_card_title(Ui *ui, StringView title);

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
JsonValue ui_form(Ui *ui, const char *name);       // a form from forms.json
void form_render(Ui *ui, JsonValue form);
// Modal with title, scrolling form body and an optional footer (buttons).
void ui_modal(Ui *ui, JsonValue form, f32 width, ModalSection footer, void *user);
void ui_dialogs(Ui *ui);                           // renders the open dialog, if any
void ui_close_dialog(Ui *ui);
void panels_render(Ui *ui, StringView name);
void layout_parse(LayoutSpec *spec, JsonValue root, f32 default_sidebar);
void layout_free(LayoutSpec *spec);
void layout_build(Ui *ui);
