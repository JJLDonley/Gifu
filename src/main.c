// CataGui - a KataGo GUI in C (Clay + raylib).
//
// Usage: catagui [file.sgf] [--theme ID] [--engine ID|none] [--portable]
//                [--move N|-1] [--screenshot out.png [--delay SECONDS]]
#include "app/app.h"
#include "base/log.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr,
            "usage: catagui [file.sgf] [--theme ID] [--engine ID|none] [--portable]\n"
            "               [--move N|-1] [--run COMMAND_ID]... [--screenshot out.png [--delay SECONDS]] [--verbose]\n");
}

static bool parse_args(int argc, char **argv, AppOptions *opts) {
    for (int i = 1; i < argc; ++i) {
        StringView a = sv(argv[i]);
        bool has_value = i + 1 < argc;
        if (sv_eq(a, SV("--theme")) && has_value) opts->theme = sv(argv[++i]);
        else if (sv_eq(a, SV("--engine")) && has_value) opts->engine = sv(argv[++i]);
        else if (sv_eq(a, SV("--screenshot")) && has_value) opts->screenshot = sv(argv[++i]);
        else if (sv_eq(a, SV("--delay")) && has_value) opts->screenshot_delay = atof(argv[++i]);
        else if (sv_eq(a, SV("--portable"))) opts->portable = true;
        else if (sv_eq(a, SV("--move")) && has_value) opts->start_move = atoi(argv[++i]);
        else if (sv_eq(a, SV("--size")) && has_value) {
            StringView rest = sv(argv[++i]);
            i64 w = 0, h = 0;
            if (!sv_parse_i64(sv_chop_char(&rest, 'x'), &w) || !sv_parse_i64(rest, &h)) return false;
            opts->window_width = (i32)w;
            opts->window_height = (i32)h;
        }
        else if (sv_eq(a, SV("--run")) && has_value && opts->run_command_count < (i32)countof(opts->run_commands)) {
            opts->run_commands[opts->run_command_count++] = sv(argv[++i]);
        }
        else if (sv_eq(a, SV("--verbose"))) log_set_level(LOG_LEVEL_DEBUG);
        else if (sv_eq(a, SV("--help")) || sv_eq(a, SV("-h"))) return false;
        else if (!sv_starts_with(a, SV("--"))) opts->sgf_path = a;
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    AppOptions options = { .screenshot_delay = 1.0 };
    if (!parse_args(argc, argv, &options)) {
        usage();
        return 2;
    }

    App *app = calloc(1, sizeof(App));
    Ui *ui = calloc(1, sizeof(Ui));
    if (!app || !ui) return 1;
    if (!app_init(app, &options)) return 1;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(options.window_width ? options.window_width : app->config.window_width,
               options.window_height ? options.window_height : app->config.window_height, "CataGui");
    SetWindowMinSize(800, 560);
    SetExitKey(KEY_NULL);
    // Pace frames to the display (smooth animations on 120/144 Hz monitors).
    i32 refresh = GetMonitorRefreshRate(GetCurrentMonitor());
    SetTargetFPS(refresh >= 30 ? refresh : 60);
    ui_init(ui, app);
    for (i32 i = 0; i < options.run_command_count; ++i) command_execute(&app->commands, options.run_commands[i]);

    f64 start = GetTime();
    while (!WindowShouldClose() && !app->quit_requested) {
        app_update(app);
        ui_frame(ui);
        if (options.screenshot.len && GetTime() - start >= options.screenshot_delay) {
            Image shot = LoadImageFromScreen();
            String path = string_from(options.screenshot);
            bool ok = ExportImage(shot, string_cstr(&path));
            UnloadImage(shot);
            log_info("screenshot %s: %s", ok ? "saved" : "FAILED", string_cstr(&path));
            string_free(&path);
            break;
        }
    }
    if (!IsWindowFullscreen() && !options.screenshot.len) {
        app->config.window_width = GetScreenWidth();
        app->config.window_height = GetScreenHeight();
    }
    ui_shutdown(ui);
    CloseWindow();
    app_shutdown(app);
    free(ui);
    free(app);
    return 0;
}
