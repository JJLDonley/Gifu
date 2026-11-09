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
