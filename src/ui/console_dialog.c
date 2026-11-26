// console_dialog.c - View -> Console (F8, or a click on the engine status): what Gifu sends its engines, their
// replies, their log (KataGo's start-up: backend, OpenCL tuning, network loading, errors) and Gifu's own log, newest
// at the bottom. A command typed below goes to the main engine as is.
#include "base/console.h"
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { SHOWN_MAX = 800 };   // the newest matching lines laid out (Clay lays out every element each frame)

static StringView kind_name(Ui *ui, ConsoleKind k) {
    switch (k) {
        case CONSOLE_SENT: return app_tr(ui->app, "console.sent", "Commands");
        case CONSOLE_REPLY: return app_tr(ui->app, "console.replies", "Replies");
        case CONSOLE_ENGINE_LOG: return app_tr(ui->app, "console.engine_log", "Engine log");
        case CONSOLE_ANALYSIS: return app_tr(ui->app, "console.analysis", "Analysis lines");
        case CONSOLE_APP: return app_tr(ui->app, "console.app_log", "Gifu log");
        default: return SV("");
    }
}

// KataGo also echoes every command and reply into its log ("...: Controller: 3 kata-analyze", "...: =3"): those
// repeat the Commands / Replies lines and are left out.
static bool engine_echo(const ConsoleLine *l) {
    if (l->kind != CONSOLE_ENGINE_LOG) return false;
    StringView t = string_sv(&l->text);
    isize colon = sv_find(t, SV(": "));
    if (colon < 0 || colon > 30) return false;
    StringView rest = sv_drop(t, colon + 2);
    return sv_starts_with(rest, SV("Controller: ")) || (rest.len > 0 && (rest.data[0] == '=' || rest.data[0] == '?') &&
                                                        rest.len > 1 && rest.data[1] >= '0' && rest.data[1] <= '9');
}

static Color kind_color(Ui *ui, ConsoleKind k) {
    const ThemeColors *c = &ui->theme->colors;
    switch (k) {
        case CONSOLE_SENT: return c->accent;
        case CONSOLE_REPLY: return c->text;
        case CONSOLE_ENGINE_LOG: return c->text_muted;
        case CONSOLE_ANALYSIS: return color_with_alpha(c->text_muted, 0.7f);
        default: return color_with_alpha(c->text, 0.8f);
    }
}

static void console_body(Ui *ui, void *user) {
    UNUSED(user);
    const ThemeColors *c = &ui->theme->colors;
    u8 *show = &ui->console_show;
    console_set_capture_analysis(*show & (1u << CONSOLE_ANALYSIS));
    // Filters and actions.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        for (i32 k = 0; k < CONSOLE_KIND_COUNT; ++k) {
            bool on = (*show >> k) & 1u;
            if (ui_button(ui, CLAY_IDI("ConsoleKind", (u32)k), kind_name(ui, (ConsoleKind)k), BUTTON_NORMAL, on, true))
                *show ^= (u8)(1u << k);
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        if (ui_button(ui, CLAY_ID("ConsoleCopy"), app_tr(ui->app, "console.copy", "Copy"), BUTTON_NORMAL, false, true)) {
            String all = { 0 };
            for (i64 s = console_first_seq(); s <= console_last_seq(); ++s) {
                ConsoleLine l;
                if (!console_get(s, &l)) continue;
                if (((*show >> l.kind) & 1u) && !engine_echo(&l))
                    string_pushf(&all, "%8.2f  %-8s %s\n", l.time, l.source, string_cstr(&l.text));
                string_free(&l.text);
            }
            SetClipboardText(string_cstr(&all));
            string_free(&all);
            app_set_status(ui->app, "Console copied");
        }
        if (ui_button(ui, CLAY_ID("ConsoleClear"), app_tr(ui->app, "console.clear", "Clear"), BUTTON_NORMAL, false, true)) console_clear();
    }
    // The lines: the newest SHOWN_MAX that pass the filters, oldest first; follows the end unless scrolled up.
    i64 last = console_last_seq(), first = console_first_seq();
    i64 picked[SHOWN_MAX];
    i32 n = 0;
    for (i64 s = last; s >= first && n < SHOWN_MAX; --s) {
        ConsoleLine l;
        if (!console_get(s, &l)) continue;
        if (((*show >> l.kind) & 1u) && !engine_echo(&l)) picked[n++] = s;
        string_free(&l.text);
    }
    Clay_ElementId list = CLAY_ID("ConsoleLines");
    f32 h = fmaxf(ui_px(ui, 200), (f32)GetScreenHeight() * 0.55f);
    CLAY({ .id = list,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .padding = CLAY_PADDING_ALL(ui_u16(ui, 8)), .childGap = ui_u16(ui, 1) },
           .backgroundColor = color_to_clay(c->surface_alt),
           .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 6)),
           .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
        if (n == 0) ui_label(ui, app_tr(ui->app, "console.empty", "Nothing yet. Engine output appears here as it starts."), FONT_UI, 13, c->text_muted);
        for (i32 i = n - 1; i >= 0; --i) {
            ConsoleLine l;
            if (!console_get(picked[i], &l)) continue;
            StringView text = arena_format(&ui->frame, "%7.2f  %-7s %s", l.time, l.source, string_cstr(&l.text));
            ui_label(ui, text, FONT_MONO, 12, kind_color(ui, l.kind));
            string_free(&l.text);
        }
        ui_scrollbar(ui, list, 210);
    }
    // Follow the newest line while the view is at the bottom (scrolling up stops it, back down resumes it).
    Clay_ScrollContainerData sc = Clay_GetScrollContainerData(list);
    if (sc.found && sc.scrollPosition) {
        f32 bottom = -(sc.contentDimensions.height - sc.scrollContainerDimensions.height);
        if (ui->wheel > 0 && Clay_PointerOver(list)) ui->console_follow = false;          // scrolled up: stay there
        else if (sc.scrollPosition->y <= bottom + 4.0f) ui->console_follow = true;        // back at the bottom: follow
        if (ui->console_follow && bottom < 0) {
            sc.scrollPosition->y = bottom;
            if (ui->scroll_id == list.id) ui->scroll_target = bottom;   // smooth_scroll's target, if it is this list
        }
        ui->console_last_seen = last;
    }
    // A command for the main engine.
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 6),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        Clay_ElementId input = CLAY_ID("ConsoleInput");
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {
            ui_text_input(ui, input, &ui->console_input, 13, 1,
                          app_tr(ui->app, "console.input", "GTP command for the engine, e.g. kata-get-param numSearchThreads"));
        }
        bool enter = ui->focus_id == input.id && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
        bool send = ui_button(ui, CLAY_ID("ConsoleSend"), app_tr(ui->app, "console.send", "Send"), BUTTON_NORMAL, false,
                              ui->console_input.text.len > 0) || enter;
        StringView cmd = sv_trim(string_sv(&ui->console_input.text));
        if (send && cmd.len) {
            if (engine_send_command(ui->app->main.engine, cmd)) text_edit_set(&ui->console_input, SV(""));
            else app_set_status(ui->app, "The engine does not take typed commands (or none is running)");
        }
    }
    ui_label(ui, app_tr(ui->app, "console.hint", "Commands that change the board (play, undo, clear_board) put the engine out of step with Gifu until the next move."),
             FONT_UI, 11, c->text_muted);
}

void console_dialog(Ui *ui) {
    ui_modal_shell(ui, app_tr(ui->app, "console.title", "Console"), 1000, console_body, NULL, NULL);
}
