// background.h - window background per theme: solid colour, user image, or a
// procedurally generated paper texture (grain, fibers, vignette, coffee rings).
#pragma once

#include "ui/theme.h"

typedef struct Background {
    Texture2D texture;   // image or generated paper; id 0 = none
    Texture2D paper;     // plain paper (no stains) used by paper-style boards
    String built_for;    // theme id the textures were built for
} Background;

// Generated paper textures are cached as PNGs in cache_dir (keyed by a hash of
// the parameters), so only the first use of a theme pays the generation cost.
void background_build(Background *bg, const Theme *theme, StringView assets_dir, StringView cache_dir);
void background_draw(const Background *bg, const Theme *theme, Rectangle area);
void background_free(Background *bg);

// Generates a paper image (CPU). Exposed for tests / tools.
Image paper_generate(i32 width, i32 height, Color base, const PaperParams *params, bool with_stains);
