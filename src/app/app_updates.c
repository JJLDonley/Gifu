// Updates glue: the daily background check, the user's choices, the restart.
#include "app/app.h"

#include "base/log.h"
#include "base/os.h"
#include "base/version_info.h"

#include <stdlib.h>
#include <time.h>

static UpdateChannel channel(const App *app) {
    return update_channel_from(string_sv(&app->config.updates_releases), SV(GIFU_VERSION));
}

static void ensure_updater(App *app) {
    if (app->updater) return;
    const char *appimage = getenv("APPIMAGE");
    app->updater = updater_create(string_sv(&app->exe_dir), sv(appimage ? appimage : ""), string_sv(&app->config.updates_url));
}

void app_updates_init(App *app) {
    bool automated = app->options.screenshot.len > 0 || app->options.headless;
    if (automated || !app->config.updates_check) return;
    char today[16];
    time_t now = time(NULL);
    strftime(today, sizeof(today), "%Y-%m-%d", localtime(&now));
    if (sv_eq(string_sv(&app->config.updates_last_check), sv(today))) return;   // once a day
    string_free(&app->config.updates_last_check);
    app->config.updates_last_check = string_from(sv(today));
    ensure_updater(app);
    updater_check(app->updater, channel(app));
}

void app_updates_check(App *app) {
    ensure_updater(app);
    app->update_manual = true;
    updater_check(app->updater, channel(app));
    app->update_dialog_request = true;   // shows "checking" and then the result
}

UpdateChannel app_updates_channel(const App *app) { return channel(app); }

void app_updates_set_channel(App *app, UpdateChannel c) {
    string_free(&app->config.updates_releases);
    app->config.updates_releases = string_from(c == UPDATE_CHANNEL_ALPHA ? SV("prerelease") : SV("stable"));
    app_updates_check(app);   // and look again on the new channel
}

void app_updates_install(App *app, StringView version) {
    if (app->updater) updater_install(app->updater, version);
}

void app_updates_skip(App *app) {
    string_free(&app->config.updates_skip);
    app->config.updates_skip = string_from(string_sv(&app->update.version));
}

void app_updates_restart(App *app) {
    if (!app->updater) return;
    String err = { 0 };
    if (updater_apply(app->updater, NULL, 0, &err)) {
        app->quit_requested = true;   // the new version is starting
    } else {
        app_set_status(app, "Update failed: %s", string_cstr(&err));
    }
    string_free(&err);
}

void app_updates_update(App *app) {
    if (!app->updater || os_time_seconds() - app->update_polled < 0.25) return;
    app->update_polled = os_time_seconds();
    UpdaterPhase before = app->update.phase;
    updater_status_free(&app->update);
    updater_status(app->updater, &app->update);
    // A background check opens the dialog only for a version the user has not skipped.
    if (app->update.phase == UPDATER_AVAILABLE && before == UPDATER_CHECKING && !app->update_manual &&
        !sv_eq(string_sv(&app->update.version), string_sv(&app->config.updates_skip))) {
        app->update_dialog_request = true;
    }
    if (before != app->update.phase && app->update.phase != UPDATER_CHECKING) app->update_manual = app->update_manual && app->update.phase != UPDATER_UP_TO_DATE;
}

void app_updates_shutdown(App *app) {
    updater_destroy(app->updater);
    app->updater = NULL;
    updater_status_free(&app->update);
    string_free(&app->update_selected);
}
