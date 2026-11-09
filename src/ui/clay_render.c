// Based on Clay's reference raylib renderer (zlib license), adapted to use the
// runtime FontCache for text and CustomElement callbacks for custom drawing.
#include "ui/clay_render.h"

#include <math.h>

Clay_Dimensions clay_measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    FontCache *fonts = user_data;
    Vector2 size = font_measure(fonts, (FontRole)config->fontId, (f32)config->fontSize, sv_from_parts(text.chars, text.length));
    if (config->lineHeight > 0) size.y = (f32)config->lineHeight;
    return (Clay_Dimensions){ size.x, size.y };
}

enum { SCISSOR_STACK_MAX = 32 };
static Rectangle g_scissor[SCISSOR_STACK_MAX];
static i32 g_scissor_depth;

static void apply_scissor(void) {
    if (g_scissor_depth == 0) {
        EndScissorMode();
        return;
    }
    Rectangle r = g_scissor[g_scissor_depth - 1];
    BeginScissorMode((i32)r.x, (i32)r.y, (i32)fmaxf(r.width, 0), (i32)fmaxf(r.height, 0));
}

void render_push_scissor(Rectangle r) {
    if (g_scissor_depth > 0) {
        Rectangle p = g_scissor[g_scissor_depth - 1];
        f32 x0 = fmaxf(r.x, p.x), y0 = fmaxf(r.y, p.y);
        f32 x1 = fminf(r.x + r.width, p.x + p.width), y1 = fminf(r.y + r.height, p.y + p.height);
        r = (Rectangle){ x0, y0, fmaxf(0, x1 - x0), fmaxf(0, y1 - y0) };
    }
    if (g_scissor_depth < SCISSOR_STACK_MAX) g_scissor[g_scissor_depth++] = r;
    apply_scissor();
}

void render_pop_scissor(void) {
    if (g_scissor_depth > 0) g_scissor_depth--;
    apply_scissor();
}

static Rectangle to_rect(Clay_BoundingBox b) {
    return (Rectangle){ roundf(b.x), roundf(b.y), roundf(b.width), roundf(b.height) };
}

static f32 roundness_for(Rectangle r, f32 radius) {
    f32 min_side = fminf(r.width, r.height);
    return min_side > 0 ? fminf(1.0f, radius * 2.0f / min_side) : 0.0f;
}

void clay_render(Clay_RenderCommandArray commands, FontCache *fonts) {
    g_scissor_depth = 0;
    for (i32 i = 0; i < commands.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&commands, i);
        Rectangle r = to_rect(cmd->boundingBox);
        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData *t = &cmd->renderData.text;
                font_draw(fonts, (FontRole)t->fontId, (f32)t->fontSize, sv_from_parts(t->stringContents.chars, t->stringContents.length),
                          (Vector2){ r.x, r.y }, clay_to_color(t->textColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                Texture2D *tex = cmd->renderData.image.imageData;
                Clay_Color tint = cmd->renderData.image.backgroundColor;
                if (tint.r == 0 && tint.g == 0 && tint.b == 0 && tint.a == 0) tint = (Clay_Color){ 255, 255, 255, 255 };
                if (tex && tex->id) {
                    DrawTexturePro(*tex, (Rectangle){ 0, 0, (f32)tex->width, (f32)tex->height }, r, (Vector2){ 0, 0 }, 0, clay_to_color(tint));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                render_push_scissor(r);
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                render_pop_scissor();
                break;
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *rect = &cmd->renderData.rectangle;
                Color c = clay_to_color(rect->backgroundColor);
                if (rect->cornerRadius.topLeft > 0) DrawRectangleRounded(r, roundness_for(r, rect->cornerRadius.topLeft), 12, c);
                else DrawRectangleRec(r, c);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *b = &cmd->renderData.border;
                Color c = clay_to_color(b->color);
                bool uniform = b->width.left == b->width.right && b->width.left == b->width.top && b->width.left == b->width.bottom;
                if (uniform && b->width.left > 0) {
                    f32 w = (f32)b->width.left;
                    if (b->cornerRadius.topLeft > 0) {
                        Rectangle inner = { r.x + w * 0.5f, r.y + w * 0.5f, r.width - w, r.height - w };
                        DrawRectangleRoundedLinesEx(inner, roundness_for(inner, b->cornerRadius.topLeft), 12, w, c);
                    } else {
                        DrawRectangleLinesEx(r, w, c);
                    }
                } else {
                    if (b->width.left) DrawRectangleRec((Rectangle){ r.x, r.y, b->width.left, r.height }, c);
                    if (b->width.right) DrawRectangleRec((Rectangle){ r.x + r.width - b->width.right, r.y, b->width.right, r.height }, c);
                    if (b->width.top) DrawRectangleRec((Rectangle){ r.x, r.y, r.width, b->width.top }, c);
                    if (b->width.bottom) DrawRectangleRec((Rectangle){ r.x, r.y + r.height - b->width.bottom, r.width, b->width.bottom }, c);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                CustomElement *custom = cmd->renderData.custom.customData;
                if (custom && custom->draw) custom->draw(custom->user, r);
                break;
            }
            default:
                break;
        }
    }
}
