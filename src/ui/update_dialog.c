// Updates dialog: every release for this platform (newest first; pre-releases
// tagged, the running version marked), the picked one's notes, and Install /
// Restart now. The Stable / Pre-releases choice decides which new version the
// daily check offers (and which is picked first here).
#include "ui/ui.h"

#include "platform/os.h"
#include "base/version_info.h"

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static bool busy(const UpdaterStatus *st) {
    return st->phase == UPDATER_CHECKING || st->phase == UPDATER_DOWNLOADING || st->phase == UPDATER_VERIFYING;
}

// The release picked in the list: the user's click, else the offered update,
// else the running version, else the newest.
static const ReleaseInfo *selected_release(Ui *ui) {
    const UpdaterStatus *st = &ui->app->update;
    StringView want[] = { string_sv(&ui->app->update_selected), string_sv(&st->version), SV(GIFU_VERSION) };
    for (i32 k = 0; k < countof(want); ++k) {
        if (!want[k].len) continue;
        vec_foreach(ReleaseInfo, r, &st->releases) {
            if (version_compare(string_sv(&r->version), want[k]) == 0) return r;
        }
    }
    return st->releases.len ? &st->releases.items[0] : NULL;
}

static void tag(Ui *ui, StringView text, Color color) {
    CLAY({ .layout = { .padding = { ui_u16(ui, 6), ui_u16(ui, 6), ui_u16(ui, 1), ui_u16(ui, 1) } },
           .backgroundColor = color_to_clay(color_with_alpha(color, 0.16f)), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
        ui_label(ui, text, FONT_UI_BOLD, 11, color);
    }
}

// Release notes are Markdown: headings in bold, list items as bullets, the rest as is.
static void notes(Ui *ui, StringView text) {
    const ThemeColors *c = &ui->theme->colors;
    StringView rest = text;
    while (rest.len) {
        StringView line = sv_trim(sv_chop_char(&rest, '\n'));
        if (!line.len) continue;
        if (line.data[0] == '#') {
            while (line.len && line.data[0] == '#') line = sv_drop(line, 1);
            ui_label(ui, sv_trim(line), FONT_UI_BOLD, 13, c->text);
        } else if (sv_starts_with(line, SV("- ")) || sv_starts_with(line, SV("* "))) {
            ui_label(ui, arena_format(&ui->frame, "•  " SV_FMT, SV_ARG(sv_trim(sv_drop(line, 2)))), FONT_UI, 13, c->text);
        } else {
            ui_label(ui, line, FONT_UI, 13, c->text);
        }
    }
}

static void release_row(Ui *ui, u32 index, const ReleaseInfo *r, bool selected, bool enabled) {
    const ThemeColors *c = &ui->theme->colors;
    Clay_ElementId id = CLAY_IDI("UpdateRelease", index);
    bool over = Clay_PointerOver(id) && enabled;
    Color bg = selected ? color_with_alpha(c->accent, 0.18f) : over ? c->hover : color_with_alpha(c->hover, 0.0f);
    bool running = version_compare(string_sv(&r->version), SV(GIFU_VERSION)) == 0;
    bool newer = version_compare(string_sv(&r->version), SV(GIFU_VERSION)) > 0;
    CLAY({ .id = id,
           .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(ui_px(ui, 30)) }, .childGap = ui_u16(ui, 8),
                       .padding = { ui_u16(ui, 10), ui_u16(ui, 10), ui_u16(ui, 4), ui_u16(ui, 4) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = color_to_clay(bg), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 4)) }) {
        ui_label(ui, string_sv(&r->version), selected ? FONT_UI_BOLD : FONT_UI, 14, c->text);
        if (r->prerelease) tag(ui, tr(ui, "update.tag_pre", "Pre-release"), c->text_muted);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        if (running) tag(ui, tr(ui, "update.tag_installed", "Installed"), c->candidate_good);
        else if (newer) tag(ui, tr(ui, "update.tag_newer", "Newer"), c->accent);
    }
    if (ui_clicked(ui, id, enabled)) {
        string_free(&ui->app->update_selected);
        ui->app->update_selected = string_from(string_sv(&r->version));
    }
}

static void update_body(Ui *ui, void *user) {
    UNUSED(user);
    const ThemeColors *c = &ui->theme->colors;
    const UpdaterStatus *st = &ui->app->update;
    bool working = busy(st);
    UpdateChannel channel = app_updates_channel(ui->app);

    // Running version | Update to: Stable / Pre-releases
    StringView running = tr(ui, "update.running", "Running");
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        ui_label(ui, arena_format(&ui->frame, SV_FMT " %s", SV_ARG(running), GIFU_VERSION), FONT_UI, 13, c->text_muted);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        ui_label(ui, tr(ui, "update.channel", "Update to"), FONT_UI, 13, c->text_muted);
        if (ui_button(ui, CLAY_ID("UpdateStable"), tr(ui, "update.channel_stable", "Stable"), BUTTON_NORMAL, channel == UPDATE_CHANNEL_STABLE, !working) &&
            channel != UPDATE_CHANNEL_STABLE) {
            app_updates_set_channel(ui->app, UPDATE_CHANNEL_STABLE);
        }
        if (ui_button(ui, CLAY_ID("UpdatePre"), tr(ui, "update.channel_pre", "Pre-releases"), BUTTON_NORMAL, channel == UPDATE_CHANNEL_ALPHA, !working) &&
            channel != UPDATE_CHANNEL_ALPHA) {
            app_updates_set_channel(ui->app, UPDATE_CHANNEL_ALPHA);
        }
    }

    // What the check found.
    switch (st->phase) {
        case UPDATER_IDLE:
        case UPDATER_CHECKING: ui_label(ui, tr(ui, "update.checking", "Checking for updates…"), FONT_UI_BOLD, 14, c->text); break;
        case UPDATER_UP_TO_DATE: ui_label(ui, tr(ui, "update.up_to_date", "Gifu is up to date."), FONT_UI_BOLD, 14, c->text); break;
        case UPDATER_AVAILABLE: {
            StringView available = tr(ui, "update.available", "New version:");
            ui_label(ui, arena_format(&ui->frame, SV_FMT " %s", SV_ARG(available), string_cstr(&st->version)), FONT_UI_BOLD, 14, c->text);
        } break;
        case UPDATER_FAILED: ui_label(ui, string_sv(&st->error), FONT_UI_BOLD, 13, c->danger); break;
        default: break;
    }

    // All releases.
    const ReleaseInfo *sel = selected_release(ui);
    if (st->releases.len) {
        ui_card_title(ui, tr(ui, "update.releases", "Releases"));
        CLAY({ .id = CLAY_ID("UpdateList"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 150)) } } }) {
            CLAY(ui_scroll_area(ui, CLAY_ID("UpdateListScroll"), 150)) {
                for (isize i = 0; i < st->releases.len; ++i) {
                    const ReleaseInfo *r = &st->releases.items[i];
                    release_row(ui, (u32)i, r, r == sel, !working && st->phase != UPDATER_READY);
                }
                ui_scrollbar(ui, CLAY_ID("UpdateListScroll"), 210);
            }
        }
    }
    // The picked release's notes.
    if (sel) {
        ui_card_title(ui, arena_format(&ui->frame, SV_FMT " %s", SV_ARG(tr(ui, "update.notes", "What changed in")), string_cstr(&sel->version)));
        CLAY({ .id = CLAY_ID("UpdateNotes"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 150)) } } }) {
            CLAY(ui_scroll_area(ui, CLAY_ID("UpdateNotesScroll"), 150)) {
                if (!sel->notes.len) ui_label(ui, tr(ui, "update.no_notes", "(no notes)"), FONT_UI, 13, c->text_muted);
                notes(ui, string_sv(&sel->notes));
                ui_scrollbar(ui, CLAY_ID("UpdateNotesScroll"), 210);
            }
        }
    }
    if (!st->can_install && st->releases.len) {
        ui_label(ui, tr(ui, "update.dev_build", "This is not an installed release: download it from the release page."), FONT_UI, 12,
                 c->text_muted);
    }

    // Progress and result.
    if (st->phase == UPDATER_DOWNLOADING || st->phase == UPDATER_VERIFYING) {
        f32 fraction = st->total > 0 ? (f32)((f64)st->done / (f64)st->total) : 0.0f;
        f32 shown = anim_towards(&ui->anim, 0x0DA7Eu, fraction, 10.0f, fraction);
        StringView what = st->phase == UPDATER_VERIFYING ? tr(ui, "update.verifying", "Verifying…") : tr(ui, "update.downloading", "Downloading…");
        ui_label(ui, arena_format(&ui->frame, SV_FMT " %s", SV_ARG(what), string_cstr(&st->installing)), FONT_UI, 13, c->text);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 6)) } },
               .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
            if (st->total > 0) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(CLAMP(shown, 0.001f, 1.0f)), CLAY_SIZING_GROW(0) } },
                       .backgroundColor = color_to_clay(c->accent), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
            }
        }
        if (st->total > 0) {
            CLAY_TEXT(ui_fmt(ui, "%.1f / %.1f MB", (f64)st->done / 1048576.0, (f64)st->total / 1048576.0), ui_text_style(ui, FONT_MONO, 12, c->text_muted));
        }
    }
    if (st->phase == UPDATER_READY) {
        StringView ready = tr(ui, "update.ready", "Downloaded and verified. Restart to finish (your games are saved).");
        ui_label(ui, arena_format(&ui->frame, "%s: " SV_FMT, string_cstr(&st->installing), SV_ARG(ready)), FONT_UI, 13, c->text);
    }
}

static void update_footer(Ui *ui, void *user) {
    UNUSED(user);
    App *app = ui->app;
    const UpdaterStatus *st = &app->update;
    UpdaterPhase phase = st->phase;
    const ReleaseInfo *sel = selected_release(ui);
    bool working = busy(st);
    if (sel && sel->page_url.len && ui_button(ui, CLAY_ID("UpdatePage"), tr(ui, "update.page", "Release page"), BUTTON_NORMAL, false, true)) {
        os_open_url(string_sv(&sel->page_url));
    }
    bool offered = phase == UPDATER_AVAILABLE && sel && version_compare(string_sv(&sel->version), string_sv(&st->version)) == 0;
    if (offered && ui_button(ui, CLAY_ID("UpdateSkip"), tr(ui, "update.skip", "Skip this version"), BUTTON_NORMAL, false, true)) {
        app_updates_skip(app);
        ui_close_dialog(ui);
    }
    bool pending = working || phase == UPDATER_AVAILABLE || phase == UPDATER_READY;
    if (ui_button(ui, CLAY_ID("UpdateLater"), pending ? tr(ui, "update.later", "Later") : tr(ui, "setup.close", "Close"), BUTTON_NORMAL, false, true)) {
        ui_close_dialog(ui);   // a download carries on in the background
    }
    if (phase == UPDATER_READY) {
        if (ui_button(ui, CLAY_ID("UpdateRestart"), tr(ui, "update.restart", "Restart now"), BUTTON_PRIMARY, false, true)) app_updates_restart(app);
    } else if (sel) {
        bool is_running = version_compare(string_sv(&sel->version), SV(GIFU_VERSION)) == 0;
        StringView install = tr(ui, "update.install", "Install");
        if (ui_button(ui, CLAY_ID("UpdateNow"), arena_format(&ui->frame, SV_FMT " %s", SV_ARG(install), string_cstr(&sel->version)), BUTTON_PRIMARY,
                      false, !working && !is_running && st->can_install)) {
            app_updates_install(app, string_sv(&sel->version));
        }
    }
}

void update_dialog(Ui *ui) {
    ui_modal_shell(ui, tr(ui, "update.title", "Updates"), 560, update_body, update_footer, NULL);
}

// Every frame: open the dialog when the app asks (a check found something, or the
// user chose Help -> Check for updates).
void update_dialog_poll(Ui *ui) {
    if (!ui->app->update_dialog_request) return;
    if (ui->dialog != UI_DIALOG_NONE && ui->dialog != UI_DIALOG_UPDATE) return;   // after the current dialog
    ui->app->update_dialog_request = false;
    if (ui->dialog == UI_DIALOG_UPDATE) return;
    string_free(&ui->app->update_selected);   // start from the offered version
    ui->dialog = UI_DIALOG_UPDATE;
    ui->focus_id = 0;
    anim_set(&ui->anim, CLAY_ID("ModalDialog").id, 0.0f);
}
