// headless.h - command-line batch analysis (no window).
#pragma once

#include "app/app.h"

// Analyses app->options.analyze_files and writes the results into the SGFs.
// Returns a process exit code.
int headless_analyze(App *app);
// --install engine,network,human [--backend ID] [--install-dir DIR]
int headless_install(App *app);
// --rank FILE.sgf...: human-network rank estimate per player.
int headless_rank(App *app);
// --capture OUT.png [--capture-area X,Y,W,H]: one screen capture (tests the capture backend).
int headless_capture(App *app);
// --read-board FILE.png | screen [--capture-area ...]: prints the grid and stones it finds.
int headless_read_board(App *app);
// --update [--channel stable|alpha]: check, download, verify and install the newest release.
int headless_update(App *app);
// --profile: builds the user's profile (profile.names, profile.folder) and prints it.
int headless_profile(App *app);
