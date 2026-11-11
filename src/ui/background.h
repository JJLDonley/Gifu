// background.h - window background per theme: solid colour, user image, or a
// procedurally generated paper texture (grain, fibers, vignette, coffee rings).
#pragma once

#include "platform/thread.h"
#include "ui/theme.h"

// A paper texture being generated on a worker thread (first use of a theme):
// the CPU work runs there, the GPU upload happens in background_poll.
typedef struct PaperJob {
    Thread thread;
    Mutex lock;
    bool running, done;   // done: guarded by lock
    bool for_board;       // Background.paper (else .texture)
    u32 generation;       // Background.generation when started (stale results are dropped)
    i32 w, h;
    Color base;
    PaperParams params;
    bool stains;
    String cache_path;
    Image image;          // result (worker -> UI thread)
} PaperJob;

typedef struct Background {
    Texture2D texture;   // image or generated paper; id 0 = none (solid colour meanwhile)
    Texture2D paper;     // plain paper (no stains) used by paper-style boards
    String built_for;    // theme id the textures were built for
    u32 generation;      // bumped by every build
    PaperJob jobs[2];
} Background;

// Generated paper textures are cached as PNGs in cache_dir (keyed by a hash of
// the parameters), so only the first use of a theme pays the generation cost.
void background_build(Background *bg, const Theme *theme, StringView assets_dir, StringView cache_dir);
void background_draw(const Background *bg, const Theme *theme, Rectangle area);
// Uploads paper generated in the background (call once per frame on the UI thread).
// Returns true when a texture appeared (callers redraw caches that use it).
bool background_poll(Background *bg);
void background_free(Background *bg);

// Generates a paper image (CPU). Exposed for tests / tools.
Image paper_generate(i32 width, i32 height, Color base, const PaperParams *params, bool with_stains);
