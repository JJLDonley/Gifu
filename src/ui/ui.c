#include "ui/ui.h"

#include "base/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Fonts ---------------------------------------------------------------------------------

typedef struct CjkOrder {
    const char *language;
    const char *faces[4];
} CjkOrder;

// Fallback order per UI language: the language's own glyph forms first.
static const CjkOrder CJK_ORDERS[] = {
    { "zh_CN", { "SC", "TC", "JP", "KR" } },
    { "zh_TW", { "TC", "SC", "JP", "KR" } },
    { "ja", { "JP", "SC", "TC", "KR" } },
    { "ko", { "KR", "SC", "TC", "JP" } },
    { "en", { "SC", "JP", "TC", "KR" } },
};

static i32 add_asset_face(Ui *ui, StringView relative, bool lazy) {
    String path = os_file_exists(relative) ? string_from(relative) : path_join(string_sv(&ui->app->assets_dir), relative);
    i32 id = font_cache_add_face(ui->fonts, string_sv(&path), lazy);
    string_free(&path);
    return id;
}

static void setup_fonts(Ui *ui) {
    const Theme *t = ui->theme;
    i32 cjk[4];
    const CjkOrder *order = &CJK_ORDERS[countof(CJK_ORDERS) - 1];
    for (i32 i = 0; i < (i32)countof(CJK_ORDERS); ++i) {
        if (sv_eq(string_sv(&ui->app->config.language), sv(CJK_ORDERS[i].language))) order = &CJK_ORDERS[i];
    }
    for (i32 i = 0; i < 4; ++i) {
        char rel[96];
        snprintf(rel, sizeof(rel), "fonts/cjk/NotoSansCJK%s-Regular.otf", order->faces[i]);
        cjk[i] = add_asset_face(ui, sv(rel), true);   // lazy: loaded on first missing glyph
    }
    i32 ui_face = add_asset_face(ui, string_sv(&t->fonts.ui), false);
    i32 bold_face = add_asset_face(ui, string_sv(&t->fonts.ui_bold), false);
    i32 mono_face = add_asset_face(ui, string_sv(&t->fonts.mono), false);
    i32 chain_ui[] = { ui_face, cjk[0], cjk[1], cjk[2], cjk[3] };
    i32 chain_bold[] = { bold_face, ui_face, cjk[0], cjk[1], cjk[2], cjk[3] };
    i32 chain_mono[] = { mono_face, ui_face, cjk[0], cjk[1], cjk[2], cjk[3] };
    font_cache_set_chain(ui->fonts, FONT_UI, chain_ui, countof(chain_ui));
    font_cache_set_chain(ui->fonts, FONT_UI_BOLD, chain_bold, countof(chain_bold));
    font_cache_set_chain(ui->fonts, FONT_MONO, chain_mono, countof(chain_mono));
    Clay_ResetMeasureTextCache();
}

// ---- Themes ---------------------------------------------------------------------------------

void ui_reload_themes(Ui *ui) {
    themes_free(&ui->themes);
    String builtin = path_join(string_sv(&ui->app->assets_dir), SV("themes"));
    String user = path_join(string_sv(&ui->app->config_dir), SV("themes"));
    StringView dirs[] = { string_sv(&builtin), string_sv(&user) };
    themes_load(&ui->themes, dirs, countof(dirs));
    string_free(&builtin);
    string_free(&user);
    log_info("loaded %td themes", ui->themes.len);
}

void ui_apply_theme(Ui *ui, isize index) {
    if (index < 0 || index >= ui->themes.len) return;
    ui->theme_index = index;
    ui->theme = &ui->themes.items[index];
    const Theme *t = ui->theme;
    String cache = path_join(string_sv(&ui->app->config_dir), SV("cache"));
    background_build(&ui->background, t, string_sv(&ui->app->assets_dir), string_sv(&cache));
    string_free(&cache);

    bool wants_texture = (t->board.style == BOARD_STYLE_WOOD || t->board.style == BOARD_STYLE_IMAGE) && t->board.texture.len;
    if (!sv_eq(string_sv(&ui->board_texture_for), string_sv(&t->board.texture)) || !wants_texture) {
        if (ui->board_texture.id) UnloadTexture(ui->board_texture);
        ui->board_texture = (Texture2D){ 0 };
        string_free(&ui->board_texture_for);
        if (wants_texture) {
            StringView tex = string_sv(&t->board.texture);
            String path = os_file_exists(tex) ? string_from(tex) : path_join(string_sv(&ui->app->assets_dir), tex);
            ui->board_texture = LoadTexture(string_cstr(&path));
            if (ui->board_texture.id) {
                GenTextureMipmaps(&ui->board_texture);
                SetTextureFilter(ui->board_texture, TEXTURE_FILTER_TRILINEAR);
            }
            ui->board_texture_for = string_from(tex);
            string_free(&path);
        }
    }
    setup_fonts(ui);
    string_free(&ui->app->config.theme);
    ui->app->config.theme = string_from(string_sv(&t->id));
}

// ---- UI commands ----------------------------------------------------------------------------

#define UI(ctx) ((Ui *)(ctx))
static void cmd_coordinates(void *c) { UI(c)->app->config.show_coordinates = !UI(c)->app->config.show_coordinates; }
static bool shows_coordinates(const void *c) { return ((const Ui *)c)->app->config.show_coordinates; }
static void cmd_next_theme(void *c) { ui_apply_theme(UI(c), (UI(c)->theme_index + 1) % UI(c)->themes.len); }
static void cmd_zoom_in(void *c) {
    UI(c)->app->config.ui_scale = MIN(UI(c)->app->config.ui_scale + 0.1f, 3.0f);
    Clay_ResetMeasureTextCache();
}
static void cmd_zoom_out(void *c) {
    UI(c)->app->config.ui_scale = MAX(UI(c)->app->config.ui_scale - 0.1f, 0.5f);
    Clay_ResetMeasureTextCache();
}
static void open_dialog(Ui *ui, UiDialog dialog) {
    ui->dialog = ui->dialog == dialog ? UI_DIALOG_NONE : dialog;
    ui->focus_id = 0;
    anim_set(&ui->anim, CLAY_ID("ModalDialog").id, 0.0f);   // replay the open animation
}
static void cmd_settings(void *c) { open_dialog(UI(c), UI_DIALOG_SETTINGS); }
static bool settings_shown(const void *c) { return ((const Ui *)c)->dialog == UI_DIALOG_SETTINGS; }
static void cmd_new_game(void *c) { open_dialog(UI(c), UI_DIALOG_NEW_GAME); }
static void cmd_theme_editor(void *c) { theme_editor_open(UI(c)); }
static void cmd_layout_editor(void *c) { layout_editor_open(UI(c)); }
static bool layout_editor_shown(const void *c) { return ((const Ui *)c)->dialog == UI_DIALOG_LAYOUT_EDITOR; }
static bool theme_editor_shown(const void *c) { return ((const Ui *)c)->dialog == UI_DIALOG_THEME_EDITOR; }
static void cmd_deep_report(void *c) { open_dialog(UI(c), UI_DIALOG_REPORT); }
static bool deep_report_shown(const void *c) { return ((const Ui *)c)->dialog == UI_DIALOG_REPORT; }
static void cmd_game_info(void *c) {
    if (UI(c)->dialog != UI_DIALOG_GAME_INFO) app_game_info_load(UI(c)->app);   // fresh copy of the SGF root
    open_dialog(UI(c), UI_DIALOG_GAME_INFO);
}
static void cmd_setup(void *c) {
    UI(c)->setup_init = false;   // re-check what is installed
    open_dialog(UI(c), UI_DIALOG_SETUP);
}
static void cmd_move_numbers(void *c) { UI(c)->app->config.show_move_numbers = !UI(c)->app->config.show_move_numbers; }
static bool shows_move_numbers(const void *c) { return ((const Ui *)c)->app->config.show_move_numbers; }
// R: start selecting a region, or clear the current one.
static void cmd_region(void *c) {
    Ui *ui = UI(c);
    if (ui->app->region.active || ui->region_select) {
        ui->region_select = ui->region_dragging = false;
        if (ui->app->region.active) app_set_region(ui->app, (BoardRegion){ 0 });
        return;
    }
    ui->region_select = true;
    app_set_status(ui->app, "Drag a rectangle on the board to limit analysis to it");
}
static bool region_on(const void *c) { return ((const Ui *)c)->app->region.active || ((const Ui *)c)->region_select; }

static void cmd_menu(void *c) { UI(c)->open_menu = UI(c)->open_menu >= 0 ? -1 : 0; }

// Cycles the first tabs component in the layout.
static void cmd_next_tab(void *c) { UI(c)->tab_selected[0]++; }
static void cmd_debug(void *c) {
    UI(c)->debug = !UI(c)->debug;
    Clay_SetDebugModeEnabled(UI(c)->debug);
}

static void register_ui_commands(Ui *ui) {
    CommandDef defs[] = {
        { "view.coordinates", "Coordinates", "Coords", cmd_coordinates, NULL, shows_coordinates, ui },
        { "view.next_theme", "Next theme", "Theme", cmd_next_theme, NULL, NULL, ui },
        { "view.zoom_in", "Zoom in", "+", cmd_zoom_in, NULL, NULL, ui },
        { "view.zoom_out", "Zoom out", "-", cmd_zoom_out, NULL, NULL, ui },
        { "view.debug", "Layout debugger", "Debug", cmd_debug, NULL, NULL, ui },
        { "view.settings", "Settings", "Settings", cmd_settings, NULL, settings_shown, ui },
        { "file.new", "New game", "New", cmd_new_game, NULL, NULL, ui },
        { "game.info", "Game info…", "Info", cmd_game_info, NULL, NULL, ui },
        { "view.theme_editor", "Edit theme…", "Theme", cmd_theme_editor, NULL, theme_editor_shown, ui },
        { "view.layout_editor", "Customize layout…", "Layout", cmd_layout_editor, NULL, layout_editor_shown, ui },
        { "analysis.report", "Deep report", "Deep report", cmd_deep_report, NULL, deep_report_shown, ui },
        { "engine.setup", "Download engines…", "Setup", cmd_setup, NULL, NULL, ui },
        { "view.next_tab", "Next tab", "Tab", cmd_next_tab, NULL, NULL, ui },
        { "view.menu", "Menu", "Menu", cmd_menu, NULL, NULL, ui },
        { "analysis.region", "Analyze region", "Region", cmd_region, NULL, region_on, ui },
        { "view.move_numbers", "Move numbers", "123", cmd_move_numbers, NULL, shows_move_numbers, ui },
    };
    command_register_all(&ui->app->commands, defs, countof(defs));
}

// ---- Lifecycle ----------------------------------------------------------------------------------

static void clay_error(Clay_ErrorData error) {
    log_warn("clay: %.*s", (int)error.errorText.length, error.errorText.chars);
}

// Warns about layout nodes / command ids that nothing registered.
typedef struct RefCheck {
    CommandRegistry *commands;
    i32 missing;
} RefCheck;

static void check_node(const Sexpr *node, void *user) {
    RefCheck *rc = user;
    StringView head = sexpr_head(node);
    if (!layout_component_exists(head)) {
        log_warn("layout:%d: unknown component '" SV_FMT "'", node->line, SV_ARG(head));
        rc->missing++;
    }
    bool is_button = sv_eq(head, SV("button")), is_menu = sv_eq(head, SV("menu"));
    if (!is_button && !is_menu) return;
    const Sexpr *first = sexpr_next_arg(node, NULL);
    for (const Sexpr *a = is_menu && first ? sexpr_next_arg(node, first) : first; a; a = is_button ? NULL : sexpr_next_arg(node, a)) {
        if (sv_eq(a->text, SV("-")) || command_find(rc->commands, a->text)) continue;
        log_warn("layout:%d: unknown command '" SV_FMT "'", a->line, SV_ARG(a->text));
        rc->missing++;
    }
}

static void validate_command_refs(Ui *ui) {
    RefCheck rc = { &ui->app->commands, 0 };
    layout_visit(ui->layout_root, check_node, &rc);
    vec_foreach(KeyBinding, kb, &ui->app->keymap.bindings) {
        if (!command_find(rc.commands, string_sv(&kb->command))) log_warn("keymap: unknown command '%s'", string_cstr(&kb->command)), rc.missing++;
    }
    // (command ID) buttons in forms.lisp.
    for (const Sexpr *form = ui->forms_root ? ui->forms_root->first : NULL; form; form = form->next) {
        for (const Sexpr *sec = sexpr_next_child(form, NULL); sec; sec = sexpr_next_child(form, sec)) {
            for (const Sexpr *f = sexpr_next_child(sec, NULL); f; f = sexpr_next_child(sec, f)) {
                const Sexpr *id = sexpr_is_list(f, "command") ? sexpr_next_arg(f, NULL) : NULL;
                if (id && !command_find(rc.commands, id->text)) {
                    log_warn("forms: unknown command '" SV_FMT "'", SV_ARG(id->text));
                    rc.missing++;
                }
            }
        }
    }
    if (rc.missing == 0) log_info("data: all layout components and command references resolve");
}

bool ui_init(Ui *ui, App *app) {
    *ui = (Ui){ .app = app, .hover_candidate = -1, .hover_list_candidate = -1, .open_menu = -1 };
    ui->frame.default_block_size = 512 * 1024;
    ui->fonts = font_cache_create();

    ui_reload_themes(ui);

    String forms_path = path_join(string_sv(&app->assets_dir), SV("data/forms.lisp"));
    String forms_text = { 0 };
    Sexpr *forms = NULL;
    SexprError err = { 0 };
    if (!os_read_file(string_sv(&forms_path), &forms_text)) {
        log_error("cannot read %s", string_cstr(&forms_path));
    } else if (!sexpr_parse(&ui->forms_arena, string_sv(&forms_text), &forms, &err)) {
        log_error("%s:%d:%d: %s", string_cstr(&forms_path), err.line, err.column, err.message);
    } else {
        ui->forms_root = forms;
    }
    string_free(&forms_text);
    string_free(&forms_path);
    ui->language_version = app->language_version;

    layout_load(ui);

    Clay_SetMaxElementCount(16384);
    u32 memory_size = Clay_MinMemorySize();
    ui->clay_memory = malloc(memory_size);
    if (!ui->clay_memory) abort();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(memory_size, ui->clay_memory);
    Clay_Initialize(arena, (Clay_Dimensions){ (f32)GetScreenWidth(), (f32)GetScreenHeight() }, (Clay_ErrorHandler){ clay_error, 0 });
    Clay_SetMeasureTextFunction(clay_measure_text, ui->fonts);
    // Culling drops off-screen clip containers but not their scrolled children,
    // which then render unclipped. The layout is small, so render everything.
    Clay_SetCullingEnabled(false);

    isize theme = themes_find(&ui->themes, string_sv(&app->config.theme));
    ui_apply_theme(ui, theme >= 0 ? theme : 0);
    register_ui_commands(ui);
    validate_command_refs(ui);

    // First run: no usable engine -> offer the downloads right away.
    bool any_engine = false;
    vec_foreach(EngineProfile, p, &app->engine_profiles) any_engine |= engine_profile_available(p);
    if (!any_engine && !sv_eq(app->options.engine, SV("none"))) ui->dialog = UI_DIALOG_SETUP;
    return true;
}

void ui_shutdown(Ui *ui) {
    theme_editor_cancel(ui);
    layout_editor_close(ui);
    text_edit_free(&ui->layout_editor.preset_name);
    text_edit_free(&ui->theme_editor.name);
    text_edit_free(&ui->theme_editor.hex);
    if (ui->installer.started) {   // stop a running download cleanly
        installer_cancel(&ui->installer);
        installer_finish(&ui->installer);
    }
    board_view_free(ui);
    background_free(&ui->background);
    if (ui->board_texture.id) UnloadTexture(ui->board_texture);
    string_free(&ui->board_texture_for);
    font_cache_destroy(ui->fonts);
    themes_free(&ui->themes);
    layout_free(ui);
    arena_free(&ui->forms_arena);
    text_edit_free(&ui->comment);
    for (i32 i = 0; i < ui->form_edit_count; ++i) text_edit_free(&ui->form_edits[i]);
    arena_free(&ui->frame);
    free(ui->clay_memory);
}

// ---- Frame ----------------------------------------------------------------------------------------

static u8 current_mods(void) {
    u8 mods = 0;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= KEYMOD_CTRL;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) mods |= KEYMOD_SHIFT;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) mods |= KEYMOD_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) mods |= KEYMOD_SUPER;
    return mods;
}

static void handle_keys(Ui *ui) {
    App *app = ui->app;
    if (ui->focus_id) return;   // a text input owns the keyboard
    if (ui->dialog != UI_DIALOG_NONE) {
        if (IsKeyPressed(KEY_ESCAPE)) ui_close_dialog(ui);
        return;
    }
    u8 mods = current_mods();
    vec_foreach(KeyBinding, b, &app->keymap.bindings) {
        if (b->chord.mods != mods) continue;
        if (IsKeyPressed(b->chord.key) || IsKeyPressedRepeat(b->chord.key)) command_execute(&app->commands, string_sv(&b->command));
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        ui->theme_menu_open = false;
        ui->open_menu = -1;
        ui->region_select = ui->region_dragging = false;
    }
}

// Wheel scrolling: each notch moves the innermost hovered scroll container's
// target by ~3 lines; the position eases towards the target every frame.
static void smooth_scroll(Ui *ui) {
    const f32 NOTCH = 60.0f * ui->scale;
    if (ui->wheel != 0.0f) {
        // Innermost hovered container that can actually scroll vertically.
        Clay_ElementIdArray over = Clay_GetPointerOverIds();
        u32 found = 0;
        for (i32 i = over.length - 1; i >= 0 && !found; --i) {
            Clay_ElementId id = over.internalArray[i];   // the Get() accessor is private to clay.h
            Clay_ScrollContainerData data = Clay_GetScrollContainerData(id);
            if (data.found && data.config.vertical && data.contentDimensions.height > data.scrollContainerDimensions.height + 1) {
                found = id.id;
                if (ui->scroll_id != found || fabsf(data.scrollPosition->y - ui->scroll_last) > 1.0f) {
                    ui->scroll_target = data.scrollPosition->y;   // new container, or moved by a drag
                }
                ui->scroll_id = found;
                f32 min_y = -(data.contentDimensions.height - data.scrollContainerDimensions.height);
                ui->scroll_target = CLAMP(ui->scroll_target + ui->wheel * NOTCH, min_y, 0.0f);
            }
        }
    }
    if (!ui->scroll_id) return;
    Clay_ElementId target = { .id = ui->scroll_id };
    Clay_ScrollContainerData data = Clay_GetScrollContainerData(target);
    if (!data.found) {
        ui->scroll_id = 0;
        return;
    }
    f32 *y = &data.scrollPosition->y;
    if (fabsf(*y - ui->scroll_last) > 1.0f && ui->wheel == 0.0f) {   // something else moved it (drag, auto-scroll)
        ui->scroll_id = 0;
        return;
    }
    f32 k = 1.0f - expf(-14.0f * ui->anim.dt);
    *y += (ui->scroll_target - *y) * k;
    if (fabsf(ui->scroll_target - *y) < 0.5f) *y = ui->scroll_target;
    ui->scroll_last = *y;
    if (*y == ui->scroll_target) ui->scroll_id = 0;   // done
}

static void handle_drops(Ui *ui) {
    if (!IsFileDropped()) return;
    FilePathList files = LoadDroppedFiles();
    if (files.count > 0) app_open_sgf(ui->app, sv(files.paths[0]));
    UnloadDroppedFiles(files);
}

static void update_title(Ui *ui) {
    static char last[512];
    char title[512];
    StringView name = ui->app->sgf_path.len ? path_basename(string_sv(&ui->app->sgf_path)) : SV("untitled");
    snprintf(title, sizeof(title), SV_FMT "%s — CataGui", SV_ARG(name), ui->app->dirty ? " •" : "");
    if (strcmp(title, last) != 0) {
        SetWindowTitle(title);
        snprintf(last, sizeof(last), "%s", title);
    }
}

void ui_frame(Ui *ui) {
    arena_reset(&ui->frame);
    anim_begin_frame(&ui->anim, GetFrameTime());
    // Effective scale = user preference x monitor content scale (150% / 200%
    // displays). Glyphs are rasterized at the final pixel size, so text stays
    // sharp; re-measure text when the scale changes (e.g. moved to another monitor).
    Vector2 dpi = GetWindowScaleDPI();
    f32 scale = ui->app->config.ui_scale * CLAMP(dpi.x, 1.0f, 4.0f);
    if (scale != ui->scale) Clay_ResetMeasureTextCache();
    ui->scale = scale;
    ui->mouse = GetMousePosition();
    ui->mouse_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    ui->mouse_released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    ui->mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    ui->wheel = GetMouseWheelMove();
    // A click outside an open help popover (and its button) closes it; uses last frame's layout.
    if (ui->help_topic[0] && ui->mouse_pressed && !Clay_PointerOver(CLAY_ID("HelpPopover")) && !ui->help_button_hovered) {
        ui->help_topic[0] = '\0';
    }
    ui->help_button_hovered = false;

    if (ui->language_version != ui->app->language_version) {
        ui->language_version = ui->app->language_version;
        setup_fonts(ui);   // CJK fallback order follows the UI language
    }
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    ui->focus_claimed = false;

    handle_drops(ui);
    handle_keys(ui);

    Clay_SetLayoutDimensions((Clay_Dimensions){ (f32)GetScreenWidth(), (f32)GetScreenHeight() });
    Clay_SetPointerState((Clay_Vector2){ ui->mouse.x, ui->mouse.y }, ui->mouse_down);
    // Clay applies wheel deltas x10 with no easing, so it only gets drags here;
    // the wheel is handled by smooth_scroll().
    Clay_UpdateScrollContainers(ui->layout_drag.kind == LAYOUT_DRAG_NONE, (Clay_Vector2){ 0, 0 }, GetFrameTime());   // no drag-scroll while moving a panel
    smooth_scroll(ui);

    background_poll(&ui->background);   // paper generated in the background (board layer key includes its texture)
    Clay_BeginLayout();
    layout_build(ui);
    Clay_RenderCommandArray commands = Clay_EndLayout();
    if (!ui->focus_claimed) ui->focus_id = 0;   // focused input disappeared
    board_view_handle_input(ui);
    if (ui->mouse_released) ui->active_id = 0;
    board_view_prepare(ui);   // render-texture work must happen outside BeginDrawing
    engine_setup_poll(ui);

    BeginDrawing();
    background_draw(&ui->background, ui->theme, (Rectangle){ 0, 0, (f32)GetScreenWidth(), (f32)GetScreenHeight() });
    clay_render(commands, ui->fonts);
    EndDrawing();
    update_title(ui);
}
