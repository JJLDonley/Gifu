// font_cache.h - runtime glyph cache with per-role fallback chains.
//
// Faces are TTF/OTF files loaded lazily (CJK faces are ~5-8 MB each and are
// only read when a glyph is not found in an earlier face). Glyphs are
// rasterized on first use with stb_truetype into GPU atlas pages, so any
// codepoint in any SGF comment can be shown without pre-baking.
#pragma once

#include "base/str.h"

#include <raylib.h>

typedef enum FontRole {
    FONT_UI = 0,
    FONT_UI_BOLD = 1,
    FONT_MONO = 2,
    FONT_ROLE_COUNT,
} FontRole;

typedef struct FontCache FontCache;

FontCache *font_cache_create(void);
void font_cache_destroy(FontCache *cache);
// Registers a face file; returns its id (or -1). Loading is deferred if lazy.
i32 font_cache_add_face(FontCache *cache, StringView path, bool lazy);
// Sets the fallback chain for a role (face ids, first wins). Clears glyphs.
void font_cache_set_chain(FontCache *cache, FontRole role, const i32 *faces, i32 count);
void font_cache_clear_glyphs(FontCache *cache);

f32 font_line_height(FontCache *cache, FontRole role, f32 size);
Vector2 font_measure(FontCache *cache, FontRole role, f32 size, StringView text);
// Draws a single line (no wrapping). `pos` is the top-left of the line box.
void font_draw(FontCache *cache, FontRole role, f32 size, StringView text, Vector2 pos, Color color);
void font_draw_centered(FontCache *cache, FontRole role, f32 size, StringView text, Vector2 center, Color color);
