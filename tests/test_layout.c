#include "test.h"

#include <stdio.h>

#include "platform/os.h"
#include "base/sexpr.h"
#include "ui/layout_model.h"

static String shipped_layout(void) {
    String path = path_join(SV(GIFU_SOURCE_DIR), SV("assets/data/layout.lisp"));
    String text = { 0 };
    CHECK(os_read_file(string_sv(&path), &text));
    string_free(&path);
    return text;
}

static void test_layout_model_roundtrip(void) {
    String text = shipped_layout();
    LayoutModel m;
    const char *err = NULL;
    CHECK(layout_model_read(string_sv(&text), &m, &err));
    CHECK(!m.sidebar_left);
    CHECK_EQ(m.sidebar_width, 400);
    CHECK_EQ(m.stack.len, 3);
    CHECK_SV(layout_item_panel(string_sv(&m.stack.items[2])), SV("winrate_graph"));
    CHECK(sv_find(string_sv(&m.stack.items[2]), SV(":height 150")) > 0);
    CHECK_EQ(m.tabs.len, 6);
    CHECK_SV(string_sv(&m.tabs.items[0]), SV("candidates"));
    CHECK_SV(string_sv(&m.tabs_props), SV(" :grow"));
    CHECK(m.toolbar.len > 5);
    CHECK_SV(layout_item_command(string_sv(&m.toolbar.items[0])), SV("file.new"));
    CHECK(layout_model_shows(&m, SV("report")));
    CHECK(!layout_model_shows(&m, SV("engine_log")));

    // Unchanged model: writing gives a file that reads back the same and keeps comments / menus.
    String same = layout_model_write(string_sv(&text), &m);
    CHECK(sv_find(string_sv(&same), SV(";; Gifu window layout.")) == 0);
    CHECK(sv_find(string_sv(&same), SV("(menu menu.file")) > 0);

    // Edit: sidebar left and wider, graph into the tabs, compare hidden, a toolbar button added and moved.
    m.sidebar_left = true;
    m.sidebar_width = 460;
    vec_push(&m.tabs, string_from(SV("winrate_graph")));
    layout_vec_remove(&m.stack, 2);
    for (isize i = 0; i < m.tabs.len; ++i) {
        if (sv_eq(string_sv(&m.tabs.items[i]), SV("compare"))) layout_vec_remove(&m.tabs, i);
    }
    vec_push(&m.toolbar, string_from(SV("(button analysis.report)")));
    CHECK_EQ(layout_vec_move(&m.toolbar, m.toolbar.len - 1, -1), m.toolbar.len - 2);
    String edited = layout_model_write(string_sv(&text), &m);

    Arena arena = { 0 };
    Sexpr *root = NULL;
    SexprError perr = { 0 };
    CHECK(sexpr_parse(&arena, string_sv(&edited), &root, &perr));
    CHECK(root && sexpr_is_list(root->first, "app"));
    arena_free(&arena);

    LayoutModel n;
    CHECK(layout_model_read(string_sv(&edited), &n, &err));
    CHECK(n.sidebar_left);
    CHECK_EQ(n.sidebar_width, 460);
    CHECK_EQ(n.stack.len, 2);
    CHECK(layout_model_shows(&n, SV("winrate_graph")));
    CHECK(!layout_model_shows(&n, SV("compare")));
    CHECK_SV(layout_item_command(string_sv(&n.toolbar.items[n.toolbar.len - 2])), SV("analysis.report"));
    CHECK(sv_find(string_sv(&edited), SV("(menu menu.view")) > 0);   // untouched parts survive
    CHECK(sv_find(string_sv(&edited), SV("(navbar")) > 0);

    layout_model_free(&n);
    layout_model_free(&m);
    string_free(&edited);
    string_free(&same);
    string_free(&text);

    // Not an editable layout.
    CHECK(!layout_model_read(SV("(app (board))"), &m, &err));
    layout_model_free(&m);
}

// Every shipped preset parses as an (app ...) layout and is editable by the model.
static void test_layout_drag_ops(void) {
    LayoutModel m = { 0 };
    const char *err = NULL;
    CHECK(layout_model_read(SV("(app (toolbar) (row :grow (board :grow) (column :width 400 (panel game_info) (separator) "
                               "(panel winrate_graph :height 150) (tabs candidates report :grow))))"),
                            &m, &err));
    // Within the stack: graph to the top (gap 0), props kept.
    CHECK(layout_model_move_panel(&m, (LayoutSlot){ false, 2 }, (LayoutSlot){ false, 0 }));
    CHECK_SV(string_sv(&m.stack.items[0]), SV("(panel winrate_graph :height 150)"));
    CHECK_SV(string_sv(&m.stack.items[1]), SV("(panel game_info)"));
    // Onto itself: no change.
    CHECK(!layout_model_move_panel(&m, (LayoutSlot){ false, 0 }, (LayoutSlot){ false, 1 }));
    // Down: game_info (1) to the end (gap 3).
    CHECK(layout_model_move_panel(&m, (LayoutSlot){ false, 1 }, (LayoutSlot){ false, 3 }));
    CHECK_SV(string_sv(&m.stack.items[2]), SV("(panel game_info)"));
    // The separator cannot become a tab.
    CHECK(!layout_model_move_panel(&m, (LayoutSlot){ false, 1 }, (LayoutSlot){ true, 0 }));
    // Stack -> tabs (name only), tabs -> stack ("(panel x)").
    CHECK(layout_model_move_panel(&m, (LayoutSlot){ false, 0 }, (LayoutSlot){ true, 1 }));
    CHECK_EQ(m.tabs.len, 3);
    CHECK_SV(string_sv(&m.tabs.items[1]), SV("winrate_graph"));
    CHECK(layout_model_move_panel(&m, (LayoutSlot){ true, 2 }, (LayoutSlot){ false, 0 }));
    CHECK_SV(string_sv(&m.stack.items[0]), SV("(panel report)"));
    CHECK(layout_model_move_panel(&m, (LayoutSlot){ true, 0 }, (LayoutSlot){ false, 0 }));
    CHECK(!layout_model_move_panel(&m, (LayoutSlot){ true, 0 }, (LayoutSlot){ false, 0 }));   // last tab stays
    CHECK(!layout_model_move_panel(&m, (LayoutSlot){ false, 9 }, (LayoutSlot){ false, 0 }));
    layout_model_free(&m);

    String s = layout_item_with_number(SV("(panel winrate_graph :height 150)"), SV("height"), 220);
    CHECK_SV(string_sv(&s), SV("(panel winrate_graph :height 220)"));
    string_free(&s);
    s = layout_item_with_number(SV("(panel winrate_graph)"), SV("height"), 90);
    CHECK_SV(string_sv(&s), SV("(panel winrate_graph :height 90)"));
    string_free(&s);
    s = layout_item_with_number(SV("(panel x :heights 3 :height 1 :grow)"), SV("height"), 5);   // whole token only
    CHECK_SV(string_sv(&s), SV("(panel x :heights 3 :height 5 :grow)"));
    string_free(&s);
}

static void test_layout_presets(void) {
    String dir = path_join(SV(GIFU_SOURCE_DIR), SV("assets/data/layouts"));
    StringVec names = { 0 };
    CHECK(os_list_dir(string_sv(&dir), &names));
    i32 presets = 0;
    vec_foreach(String, n, &names) {
        if (!sv_ends_with(string_sv(n), SV(".lisp"))) continue;
        String path = path_join(string_sv(&dir), string_sv(n));
        String text = { 0 };
        CHECK(os_read_file(string_sv(&path), &text));
        Arena arena = { 0 };
        Sexpr *root = NULL;
        SexprError err = { 0 };
        bool parsed = sexpr_parse(&arena, string_sv(&text), &root, &err);
        if (!parsed) fprintf(stderr, "    %s:%d: %s\n", string_cstr(n), err.line, err.message);
        CHECK(parsed && root && sexpr_is_list(root->first, "app"));
        LayoutModel m;
        const char *why = NULL;
        CHECK(layout_model_read(string_sv(&text), &m, &why));
        for (isize i = 0; i < m.tabs.len; ++i) {   // only real panels
            bool known = false;
            for (isize p = 0; p < LAYOUT_PANEL_COUNT; ++p) known |= sv_eq(string_sv(&m.tabs.items[i]), sv(LAYOUT_PANELS[p]));
            CHECK(known);
        }
        layout_model_free(&m);
        arena_free(&arena);
        string_free(&text);
        string_free(&path);
        presets++;
    }
    CHECK(presets >= 3);
    string_vec_free(&names);
    string_free(&dir);
}

void register_layout_tests(void) {
    test_register("layout/presets", test_layout_presets); test_register("layout/drag_ops", test_layout_drag_ops); test_register("layout/model_roundtrip", test_layout_model_roundtrip); }
