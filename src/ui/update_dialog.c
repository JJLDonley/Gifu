// Update dialog: what the updater found, its release notes, and the user's choice
// (Update / Later / Skip this version), then download progress and Restart.
#include "ui/ui.h"

#include "base/os.h"
#include "base/version_info.h"

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static void update_body(Ui *ui, void *user) {
    UNUSED(user);
    const ThemeColors *c = &ui->theme->colors;
    const UpdaterStatus *st = &ui->app->update;
    StringView running = tr(ui, "update.running", "Running");
    bool busy = st->phase == UPDATER_DOWNLOADING || st->phase == UPDATER_VERIFYING;
    UpdateChannel channel = app_updates_channel(ui->app);
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 8),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        ui_label(ui, arena_format(&ui->frame, SV_FMT " %s", SV_ARG(running), GIFU_VERSION), FONT_UI, 13, c->text_muted);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } } }) {}
        ui_label(ui, tr(ui, "update.channel", "Update to"), FONT_UI, 13, c->text_muted);
        if (ui_button(ui, CLAY_ID("UpdateStable"), tr(ui, "update.channel_stable", "Stable"), BUTTON_NORMAL, channel == UPDATE_CHANNEL_STABLE, !busy) &&
            channel != UPDATE_CHANNEL_STABLE) {
            app_updates_set_channel(ui->app, UPDATE_CHANNEL_STABLE);
        }
        if (ui_button(ui, CLAY_ID("UpdatePre"), tr(ui, "update.channel_pre", "Pre-releases"), BUTTON_NORMAL, channel == UPDATE_CHANNEL_ALPHA, !busy) &&
            channel != UPDATE_CHANNEL_ALPHA) {
            app_updates_set_channel(ui->app, UPDATE_CHANNEL_ALPHA);
        }
    }
    switch (st->phase) {
        case UPDATER_IDLE:
        case UPDATER_CHECKING: ui_label(ui, tr(ui, "update.checking", "Checking for updates…"), FONT_UI_BOLD, 14, c->text); break;
        case UPDATER_UP_TO_DATE: ui_label(ui, tr(ui, "update.up_to_date", "Gifu is up to date."), FONT_UI_BOLD, 14, c->text); break;
        case UPDATER_FAILED: ui_label(ui, string_sv(&st->error), FONT_UI_BOLD, 13, c->danger); break;
        default: {
            StringView available = tr(ui, "update.available", "New version:");
            ui_label(ui, arena_format(&ui->frame, SV_FMT " %s", SV_ARG(available), string_cstr(&st->version)), FONT_UI_BOLD, 15, c->text);
            if (st->notes.len) {
                CLAY({ .id = CLAY_ID("UpdateNotes"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 220)) } } }) {
                    CLAY(ui_scroll_area(ui, CLAY_ID("UpdateNotesScroll"), 220)) {
                        ui_label(ui, string_sv(&st->notes), FONT_UI, 13, c->text);
                        ui_scrollbar(ui, CLAY_ID("UpdateNotesScroll"), 210);
                    }
                }
            }
            if (!st->can_install && st->phase == UPDATER_AVAILABLE) {
                ui_label(ui, tr(ui, "update.dev_build", "This is not an installed release: download it from the release page."), FONT_UI, 12,
                         c->text_muted);
            }
            if (st->phase == UPDATER_DOWNLOADING || st->phase == UPDATER_VERIFYING) {
                f32 fraction = st->total > 0 ? (f32)((f64)st->done / (f64)st->total) : 0.0f;
                f32 shown = anim_towards(&ui->anim, 0x0DA7Eu, fraction, 10.0f, fraction);
                ui_label(ui, st->phase == UPDATER_VERIFYING ? tr(ui, "update.verifying", "Verifying…") : tr(ui, "update.downloading", "Downloading…"),
                         FONT_UI, 13, c->text);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 6)) } },
                       .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
                    if (st->total > 0) {
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(CLAMP(shown, 0.001f, 1.0f)), CLAY_SIZING_GROW(0) } },
                               .backgroundColor = color_to_clay(c->accent), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
                    }
                }
                if (st->total > 0) {
                    CLAY_TEXT(ui_fmt(ui, "%.1f / %.1f MB", (f64)st->done / 1048576.0, (f64)st->total / 1048576.0),
                              ui_text_style(ui, FONT_MONO, 12, c->text_muted));
                }
            }
            if (st->phase == UPDATER_READY) {
                ui_label(ui, tr(ui, "update.ready", "Downloaded and verified. Restart to finish (your games are saved)."), FONT_UI, 13, c->text);
            }
        } break;
    }
}

static void update_footer(Ui *ui, void *user) {
    UNUSED(user);
    App *app = ui->app;
    UpdaterPhase phase = app->update.phase;
    bool busy = phase == UPDATER_DOWNLOADING || phase == UPDATER_VERIFYING || phase == UPDATER_CHECKING;
    if (app->update.page_url.len && ui_button(ui, CLAY_ID("UpdatePage"), tr(ui, "update.page", "Release page"), BUTTON_NORMAL, false, true)) {
        os_open_url(string_sv(&app->update.page_url));
    }
    if (phase == UPDATER_AVAILABLE && ui_button(ui, CLAY_ID("UpdateSkip"), tr(ui, "update.skip", "Skip this version"), BUTTON_NORMAL, false, true)) {
        app_updates_skip(app);
        ui_close_dialog(ui);
    }
    if (ui_button(ui, CLAY_ID("UpdateLater"), busy || phase == UPDATER_AVAILABLE || phase == UPDATER_READY ? tr(ui, "update.later", "Later")
                                                                                                         : tr(ui, "setup.close", "Close"),
                  BUTTON_NORMAL, false, true)) {
        ui_close_dialog(ui);   // a download carries on in the background
    }
    if (phase == UPDATER_AVAILABLE &&
        ui_button(ui, CLAY_ID("UpdateNow"), tr(ui, "update.update", "Update"), BUTTON_PRIMARY, false, app->update.can_install)) {
        app_updates_download(app);
    }
    if (phase == UPDATER_READY && ui_button(ui, CLAY_ID("UpdateRestart"), tr(ui, "update.restart", "Restart now"), BUTTON_PRIMARY, false, true)) {
        app_updates_restart(app);
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
    ui->dialog = UI_DIALOG_UPDATE;
    ui->focus_id = 0;
    anim_set(&ui->anim, CLAY_ID("ModalDialog").id, 0.0f);
}
