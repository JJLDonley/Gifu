// Gifu - a KataGo GUI in C (Clay + raylib).
//
// Usage: gifu [file.sgf] [--theme ID] [--engine ID|none] [--portable]
//                [--move N|-1] [--screenshot out.png [--delay SECONDS]]
#include "app/app.h"
#include "app/headless.h"
#include "base/version_info.h"
#include "base/log.h"
#include "base/os.h"
#include "ui/ui.h"
#include "ui/area_select.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(stderr,
            "usage: gifu [file.sgf] [--theme ID] [--engine ID|none] [--portable] [--size WxH]\n"
            "               [--move N|-1] [--region A1:G7] [--set KEY=VALUE]... [--run COMMAND_ID]...\n"
            "               [--screenshot out.png [--delay SECONDS]] [--verbose]\n"
            "       gifu --analyze FILE.sgf... [--out DIR] [--engine ID] [--set analysis.game_visits=N]\n"
            "       gifu --rank FILE.sgf...   (estimate player ranks with the human SL network)\n"
            "       gifu --update [--channel stable|alpha]   (install the newest release)\n"
            "       gifu --version\n"
            "       gifu --read-board FILE.png|screen [--capture-area X,Y,W,H] [--size 19]   (board recognition)\n"
            "       gifu --capture OUT.png [--capture-area X,Y,W,H] [--set sync.capture=auto|gdi|x11|portal|grim|command]\n"
            "       gifu --install engine,network,human|all [--backend opencl|eigenavx2|eigen|cuda12.8-cudnn9.8.0] [--install-dir DIR]\n");
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
        else if (sv_eq(a, SV("--out")) && has_value) opts->output_dir = sv(argv[++i]);
        else if (sv_eq(a, SV("--region")) && has_value) opts->region = sv(argv[++i]);
        else if (sv_eq(a, SV("--capture")) && has_value) opts->capture = sv(argv[++i]), opts->headless = true;
        else if (sv_eq(a, SV("--capture-area")) && has_value) opts->capture_area = sv(argv[++i]);
        else if (sv_eq(a, SV("--read-board")) && has_value) opts->read_board = sv(argv[++i]), opts->headless = true;
        else if (sv_eq(a, SV("--update"))) opts->update = true, opts->headless = true;
        else if (sv_eq(a, SV("--channel")) && has_value) opts->update_channel = sv(argv[++i]);
        else if (sv_eq(a, SV("--updated-from")) && has_value) opts->updated_from = atoll(argv[++i]);
        else if (sv_eq(a, SV("--version"))) {
            printf("Gifu %s\n", GIFU_VERSION);
            exit(0);
        }
        else if (sv_eq(a, SV("--window-pos")) && has_value) {
            StringView rest = sv(argv[++i]);
            i64 x = 0, y = 0;
            if (!sv_parse_i64(sv_chop_char(&rest, ','), &x) || !sv_parse_i64(rest, &y)) return false;
            opts->window_pos_set = true, opts->window_x = (i32)x, opts->window_y = (i32)y;
        }
        else if (sv_eq(a, SV("--install")) && has_value) opts->install = sv(argv[++i]), opts->headless = true;
        else if (sv_eq(a, SV("--backend")) && has_value) opts->install_backend = sv(argv[++i]);
        else if (sv_eq(a, SV("--install-dir")) && has_value) opts->install_dir = sv(argv[++i]);
        else if (sv_eq(a, SV("--analyze")) || sv_eq(a, SV("--rank"))) {
            opts->rank = sv_eq(a, SV("--rank"));
            // All following non-option arguments are SGF files.
            while (i + 1 < argc && argv[i + 1][0] != '-' && opts->analyze_count < (i32)countof(opts->analyze_files)) {
                opts->analyze_files[opts->analyze_count++] = sv(argv[++i]);
            }
            opts->headless = true;
        }
        else if (sv_eq(a, SV("--set")) && has_value && opts->setting_count < (i32)countof(opts->settings)) {
            opts->settings[opts->setting_count++] = sv(argv[++i]);
        }
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

// --select-area X,Y,W,H [--select-hint TEXT]: the on-screen board picker (run by the app as a helper).
static int select_area_main(int argc, char **argv) {
    i64 v[4] = { 0 };
    const char *hint = "Drag a box around the board  -  Esc cancels";
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--select-hint") == 0) hint = argv[i + 1];
        if (strcmp(argv[i], "--select-area") != 0) continue;
        StringView rest = sv(argv[i + 1]);
        for (int k = 0; k < 4; ++k) {
            if (!sv_parse_i64(sv_trim(sv_chop_char(&rest, ',')), &v[k])) return 2;
        }
    }
    if (v[2] <= 0 || v[3] <= 0) return 2;
    return area_select_run((i32)v[0], (i32)v[1], (i32)v[2], (i32)v[3], hint);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--select-area") == 0) return select_area_main(argc, argv);
    }
    AppOptions options = { .screenshot_delay = 1.0 };
    if (!parse_args(argc, argv, &options)) {
        usage();
        return 2;
    }

    App *app = calloc(1, sizeof(App));
    if (options.headless) {   // batch analysis from the command line, no window
        if (!app || !app_init(app, &options)) return 1;
        int code = options.install.len   ? headless_install(app)
                   : options.capture.len ? headless_capture(app)
                   : options.read_board.len ? headless_read_board(app)
                   : options.update      ? headless_update(app)
                   : options.rank        ? headless_rank(app)
                                         : headless_analyze(app);
        app_shutdown(app);
        free(app);
        return code;
    }
    Ui *ui = calloc(1, sizeof(Ui));
    if (!app || !ui) return 1;
    if (!app_init(app, &options)) return 1;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(options.window_width ? options.window_width : app->config.window_width,
               options.window_height ? options.window_height : app->config.window_height, "Gifu");
    // Start at a size that fits high-DPI monitors (config sizes are at 100%).
    Vector2 dpi = GetWindowScaleDPI();
    if (dpi.x > 1.01f && !options.window_width) {
        i32 w = (i32)((f32)app->config.window_width * dpi.x), h = (i32)((f32)app->config.window_height * dpi.y);
        i32 monitor = GetCurrentMonitor();
        w = MIN(w, GetMonitorWidth(monitor) - 40);
        h = MIN(h, GetMonitorHeight(monitor) - 80);
        SetWindowSize(w, h);
    }
    SetWindowMinSize(800, 560);
    if (options.window_pos_set) SetWindowPosition(options.window_x, options.window_y);
    // Window / taskbar icon (the Windows exe also embeds it as a resource).
    String icon_path = path_join(string_sv(&app->assets_dir), SV("icons/gifu.png"));
    Image icon = LoadImage(string_cstr(&icon_path));
    if (icon.data) {
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
    string_free(&icon_path);
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
        Vector2 dpi_now = GetWindowScaleDPI();
        app->config.window_width = (i32)((f32)GetScreenWidth() / MAX(dpi_now.x, 1.0f));
        app->config.window_height = (i32)((f32)GetScreenHeight() / MAX(dpi_now.y, 1.0f));
    }
    ui_shutdown(ui);
    CloseWindow();
    app_shutdown(app);
    free(ui);
    free(app);
    return 0;
}
