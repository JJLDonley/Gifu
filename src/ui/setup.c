// Engine setup dialog: download KataGo, the strongest network and the human
// SL network (app/installer.c does the work on a background thread).
#include "ui/ui.h"

#include <math.h>
#include <stdio.h>

static StringView tr(Ui *ui, const char *key, const char *fallback) { return app_tr(ui->app, key, fallback); }

static const KataGoBackend *current_backend(Ui *ui) {
    const KataGoBackend *b = katago_backend_find(string_sv(&ui->app->config.install_backend));
    return b ? b : katago_backend_at(0);
}

// Shortens `text` from the left ("…/end/of/path") until it fits `max_width`.
static StringView elide_left(Ui *ui, StringView text, f32 size, f32 max_width) {
    f32 px = ui_px(ui, size);
    if (font_measure(ui->fonts, FONT_MONO, px, text).x <= max_width) return text;
    StringView tail = text;
    while (tail.len > 4) {
        tail = sv_drop(tail, 1);
        while (tail.len && ((u8)tail.data[0] & 0xC0) == 0x80) tail = sv_drop(tail, 1);   // stay on a UTF-8 boundary
        StringView candidate = arena_format(&ui->frame, "…" SV_FMT, SV_ARG(tail));
        if (font_measure(ui->fonts, FONT_MONO, px, candidate).x <= max_width) return candidate;
    }
    return tail;
}

static void item_row(Ui *ui, u32 index, bool *selected, bool installed, StringView title, StringView detail, bool running) {
    const ThemeColors *c = &ui->theme->colors;
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = ui_u16(ui, 12),
                       .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
        if (ui_toggle(ui, CLAY_IDI("SetupToggle", index), *selected) && !running) *selected = !*selected;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
            ui_label(ui, title, FONT_UI_BOLD, 14, c->text);
            ui_label(ui, detail, FONT_UI, 12, c->text_muted);
        }
        ui_label(ui, installed ? tr(ui, "setup.installed", "Installed") : tr(ui, "setup.missing", "Not installed"), FONT_UI, 12,
                 installed ? c->candidate_good : c->text_muted);
    }
}

static void setup_body(Ui *ui, void *user) {
    UNUSED(user);
    App *app = ui->app;
    const ThemeColors *c = &ui->theme->colors;
    String dir = app_katago_dir(app);
    const KataGoBackend *backend = current_backend(ui);
    InstallStatus status = installer_detect(string_sv(&dir), backend);
    InstallState st = installer_state(&ui->installer);
    bool running = st.running;
    if (!ui->setup_init) {   // preselect whatever is missing
        ui->setup_engine = !status.engine;
        ui->setup_network = !status.network;
        ui->setup_human = !status.human;
        ui->setup_init = true;
    }

    ui_label(ui, tr(ui, "setup.intro", "Gifu uses KataGo to analyse games. Files are installed into"), FONT_UI, 13, c->text_muted);
    ui_label(ui, elide_left(ui, string_sv(&dir), 12, ui_px(ui, 600 - 48)), FONT_MONO, 12, c->text);

    ui_card_title(ui, tr(ui, "setup.backend", "Engine build"));
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = ui_u16(ui, 6) } }) {
        for (isize i = 0; i < katago_backend_count(); ++i) {
            const KataGoBackend *b = katago_backend_at(i);
            bool selected = b == backend;
            if (ui_button(ui, CLAY_IDI("SetupBackend", (u32)i), sv(b->label), BUTTON_NORMAL, selected, !running) && !selected) {
                config_set_string(&app->config, config_field(SV("install.backend")), sv(b->id));
                ui->setup_init = false;   // re-evaluate what is missing for this build
            }
        }
    }

    ui_card_title(ui, tr(ui, "setup.downloads", "Downloads"));
    item_row(ui, 0, &ui->setup_engine, status.engine, tr(ui, "setup.engine", "KataGo engine"),
             tr(ui, "setup.engine_detail", "Latest release for the build above (~15-500 MB)"), running);
    item_row(ui, 1, &ui->setup_network, status.network, tr(ui, "setup.network", "Strongest network"),
             tr(ui, "setup.network_detail", "Strongest b18c384nbt (fast on any GPU) from katagotraining.org (~100 MB)"), running);
    item_row(ui, 2, &ui->setup_human, status.human, tr(ui, "setup.human", "Human SL network"),
             tr(ui, "setup.human_detail", "Plays and evaluates like a human of a chosen rank (~100 MB)"), running);
    if (status.human) {
        ui_label(ui, tr(ui, "setup.human_hint", "Use the \"KataGo · Human style\" engine profile; pick its strength in Settings."), FONT_UI,
                 12, c->text_muted);
    }

    // Progress / result.
    if (ui->installer.started || st.finished) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                           .childGap = ui_u16(ui, 6), .padding = { 0, 0, ui_u16(ui, 6), 0 } } }) {
            Color color = st.failed ? c->danger : c->text;
            ui_label(ui, sv(st.failed ? st.error : st.label), FONT_UI_BOLD, 13, color);
            if (running) {
                f32 fraction = st.total > 0 ? (f32)((f64)st.done / (f64)st.total) : 0.0f;
                f32 shown = anim_towards(&ui->anim, 0x5E7u, fraction, 10.0f, fraction);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ui_px(ui, 6)) } },
                       .backgroundColor = color_to_clay(c->surface_alt), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {
                    if (st.total > 0) {
                        CLAY({ .layout = { .sizing = { CLAY_SIZING_PERCENT(CLAMP(shown, 0.001f, 1.0f)), CLAY_SIZING_GROW(0) } },
                               .backgroundColor = color_to_clay(c->accent), .cornerRadius = CLAY_CORNER_RADIUS(ui_px(ui, 3)) }) {}
                    }
                }
                if (st.done > 0) {
                    CLAY_TEXT(st.total > 0 ? ui_fmt(ui, "%.1f / %.1f MB", (f64)st.done / 1048576.0, (f64)st.total / 1048576.0)
                                           : ui_fmt(ui, "%.1f MB", (f64)st.done / 1048576.0),
                              ui_text_style(ui, FONT_MONO, 12, c->text_muted));
                }
            }
        }
    }
    string_free(&dir);
}

static void setup_footer(Ui *ui, void *user) {
    UNUSED(user);
    InstallState st = installer_state(&ui->installer);
    if (st.running) {
        if (ui_button(ui, CLAY_ID("SetupCancel"), tr(ui, "label.cancel", "Cancel"), BUTTON_NORMAL, false, true)) installer_cancel(&ui->installer);
        return;   // (outside any CLAY body)
    }
    if (ui_button(ui, CLAY_ID("SetupClose"), tr(ui, "setup.close", "Close"), BUTTON_NORMAL, false, true)) ui_close_dialog(ui);
    bool any = ui->setup_engine || ui->setup_network || ui->setup_human;
    if (ui_button(ui, CLAY_ID("SetupDownload"), tr(ui, "setup.download", "Download"), BUTTON_PRIMARY, false, any)) {
        String dir = app_katago_dir(ui->app);
        installer_start(&ui->installer, string_sv(&dir), current_backend(ui), ui->setup_engine, ui->setup_network, ui->setup_human);
        ui->setup_reloaded = false;
        string_free(&dir);
    }
}

// Every frame (dialog open or not): once an install succeeds, reload the
// engine profiles and start KataGo.
void engine_setup_poll(Ui *ui) {
    if (!ui->installer.started) return;
    InstallState st = installer_state(&ui->installer);
    if (!st.finished || ui->setup_reloaded) return;
    ui->setup_reloaded = true;
    installer_finish(&ui->installer);
    if (st.failed) return;
    app_reload_engines(ui->app);
    ui->setup_init = false;
    app_set_status(ui->app, "KataGo is installed and starting");
}

void engine_setup_dialog(Ui *ui) {
    ui_modal_shell(ui, tr(ui, "setup.title", "Engine setup"), 600, setup_body, setup_footer, NULL);
}
