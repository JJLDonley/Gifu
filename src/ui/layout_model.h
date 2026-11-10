// layout_model.h - the editable part of layout.lisp: the toolbar and the
// sidebar (panel stack + tab group, side and width).
//
// Reading finds those nodes in the layout text; writing splices regenerated
// text for just those nodes back into the original, so menus, the navigation
// bar and every comment stay exactly as the user (or the app) wrote them.
// Items are kept as their original s-expression text, so props survive moves.
// Pure (no raylib): unit-tested.
#pragma once

#include "base/str.h"
#include "base/os.h"    // StringVec
#include "base/vec.h"

typedef struct LayoutModel {
    bool sidebar_left;    // sidebar before the board in the row
    i32 sidebar_width;
    StringVec stack;      // "(panel winrate_graph :height 150)" ...
    StringVec tabs;       // panel names in the tab group
    StringVec toolbar;    // "(button file.new :icon @plus :label)", "(separator)" ...

    // Where the nodes are in the source text (for writing back).
    isize toolbar_start, toolbar_end;
    isize row_start, row_end;
    String row_header;      // "(row :grow" (head + props, without children)
    String board_text;      // "(board :grow)"
    String column_header;   // "(column :width 400 :scroll ..." (head + props)
    String tabs_props;      // " :grow"
} LayoutModel;

// Every panel the app can show (panels.c).
extern const char *const LAYOUT_PANELS[];
extern const isize LAYOUT_PANEL_COUNT;

// False (with a reason) when the layout has no (toolbar) or no (row (board) (column ... (tabs ...))).
bool layout_model_read(StringView text, LayoutModel *out, const char **error);
String layout_model_write(StringView original, const LayoutModel *model);
void layout_model_free(LayoutModel *model);

bool layout_model_shows(const LayoutModel *model, StringView panel);   // in the stack or the tabs
StringView layout_item_panel(StringView item);                          // "(panel x ...)" -> "x"
StringView layout_item_command(StringView item);                        // "(button cmd ...)" -> "cmd"
// A place in the sidebar: an item of the panel stack, or a tab of the tab group.
// As a drop target, `index` is a gap (0..len), counted before the item is removed.
typedef struct LayoutSlot {
    bool tabs;
    isize index;
} LayoutSlot;
// Moves a panel between / within the stack and the tab group (drag and drop).
// Stack items keep their props when they stay in the stack; a panel entering
// the stack becomes "(panel name)". Returns false when nothing changes or the
// move is not allowed (a non-panel item into the tabs, the last tab out).
bool layout_model_move_panel(LayoutModel *model, LayoutSlot from, LayoutSlot to);
// `item` with ":key value" set (replaced, or added before the closing paren).
String layout_item_with_number(StringView item, StringView key, i32 value);

// Moves element `index` of `vec` by `delta` (clamped). Returns the new index.
isize layout_vec_move(StringVec *vec, isize index, isize delta);
void layout_vec_remove(StringVec *vec, isize index);
