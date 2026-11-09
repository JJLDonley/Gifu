// clay_render.h - draws Clay render commands with raylib + FontCache.
#pragma once

#include "ui/font_cache.h"

#include <clay.h>

// Custom elements carry a pointer to this (allocated in the frame arena).
typedef void (*CustomDrawFn)(void *user, Rectangle bounds);
typedef struct CustomElement {
    CustomDrawFn draw;
    void *user;
} CustomElement;

// Nested clipping: each push intersects with the current clip rectangle and
// pop restores the parent's. Custom draw callbacks must use these instead of
// raylib's Begin/EndScissorMode so they don't cancel an enclosing clip.
void render_push_scissor(Rectangle r);
void render_pop_scissor(void);

Clay_Dimensions clay_measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data);
void clay_render(Clay_RenderCommandArray commands, FontCache *fonts);

static inline Color clay_to_color(Clay_Color c) { return (Color){ (u8)c.r, (u8)c.g, (u8)c.b, (u8)c.a }; }
static inline Clay_Color color_to_clay(Color c) { return (Clay_Color){ c.r, c.g, c.b, c.a }; }
