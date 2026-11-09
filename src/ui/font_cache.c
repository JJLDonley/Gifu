#include "ui/font_cache.h"

#include "base/log.h"
#include "base/os.h"

#include <math.h>
#include <stb_truetype.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_FACES = 16,
    MAX_CHAIN = 8,
    MAX_PAGES = 16,
    PAGE_SIZE = 1024,
    GLYPH_PADDING = 1,
    INITIAL_SLOTS = 4096,
};

typedef struct FontFace {
    String path;
    String data;
    stbtt_fontinfo info;
    bool loaded, failed;
    i32 ascent, descent, line_gap;   // font units
    f32 units_per_em;
} FontFace;

typedef struct AtlasPage {
    Texture2D texture;
    i32 shelf_x, shelf_y, shelf_h;
} AtlasPage;

typedef struct Glyph {
    u64 key;   // 0 = empty slot
    i16 page;  // -1 = no bitmap (space / missing)
    u16 x, y, w, h;
    f32 x_offset, y_offset;   // from pen position on baseline
    f32 advance;
    bool missing;
} Glyph;

struct FontCache {
    FontFace faces[MAX_FACES];
    i32 face_count;
    i32 chains[FONT_ROLE_COUNT][MAX_CHAIN];
    i32 chain_len[FONT_ROLE_COUNT];
    AtlasPage pages[MAX_PAGES];
    i32 page_count;
    Glyph *slots;
    isize slot_count;
    isize glyph_count;
    u8 *scratch;   // rasterization buffer
    isize scratch_size;
};

// ---- Faces ----------------------------------------------------------------------------

static bool face_ensure_loaded(FontFace *face) {
    if (face->loaded) return true;
    if (face->failed) return false;
    if (!os_read_file(string_sv(&face->path), &face->data)) {
        log_warn("font: cannot read %s", string_cstr(&face->path));
        face->failed = true;
        return false;
    }
    const u8 *bytes = (const u8 *)face->data.data;
    int offset = stbtt_GetFontOffsetForIndex(bytes, 0);
    if (offset < 0 || !stbtt_InitFont(&face->info, bytes, offset)) {
        log_warn("font: cannot parse %s", string_cstr(&face->path));
        face->failed = true;
        string_free(&face->data);
        return false;
    }
    stbtt_GetFontVMetrics(&face->info, &face->ascent, &face->descent, &face->line_gap);
    face->units_per_em = 1.0f / stbtt_ScaleForMappingEmToPixels(&face->info, 1.0f);
    face->loaded = true;
    log_info("font: loaded %s (%.1f MB)", string_cstr(&face->path), (f64)face->data.len / (1024.0 * 1024.0));
    return true;
}

static f32 face_scale(const FontFace *face, f32 size) { return size / face->units_per_em; }

// ---- Hash table -------------------------------------------------------------------------

static u64 glyph_key(FontRole role, u32 codepoint, f32 size) {
    u32 px = (u32)CLAMP(lroundf(size * 4.0f), 1, 0xFFFF);   // quarter-pixel size buckets
    return ((u64)(role + 1) << 56) | ((u64)px << 32) | codepoint;
}

static u64 hash_u64(u64 x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    return x;
}

static void table_grow(FontCache *cache) {
    isize new_count = cache->slot_count ? cache->slot_count * 2 : INITIAL_SLOTS;
    Glyph *slots = calloc((usize)new_count, sizeof(Glyph));
    if (!slots) abort();
    for (isize i = 0; i < cache->slot_count; ++i) {
        Glyph g = cache->slots[i];
        if (!g.key) continue;
        isize j = (isize)(hash_u64(g.key) & (u64)(new_count - 1));
        while (slots[j].key) j = (j + 1) & (new_count - 1);
        slots[j] = g;
    }
    free(cache->slots);
    cache->slots = slots;
    cache->slot_count = new_count;
}

static Glyph *table_find(FontCache *cache, u64 key) {
    isize mask = cache->slot_count - 1;
    isize j = (isize)(hash_u64(key) & (u64)mask);
    while (cache->slots[j].key) {
        if (cache->slots[j].key == key) return &cache->slots[j];
        j = (j + 1) & mask;
    }
    return NULL;
}

static Glyph *table_insert(FontCache *cache, Glyph g) {
    if ((cache->glyph_count + 1) * 2 > cache->slot_count) table_grow(cache);
    isize mask = cache->slot_count - 1;
    isize j = (isize)(hash_u64(g.key) & (u64)mask);
    while (cache->slots[j].key) j = (j + 1) & mask;
    cache->slots[j] = g;
    cache->glyph_count++;
    return &cache->slots[j];
}

// ---- Atlas --------------------------------------------------------------------------------

static AtlasPage *page_new(FontCache *cache) {
    if (cache->page_count >= MAX_PAGES) return NULL;
    Image img = GenImageColor(PAGE_SIZE, PAGE_SIZE, (Color){ 255, 255, 255, 0 });
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA);
    AtlasPage *page = &cache->pages[cache->page_count++];
    page->texture = LoadTextureFromImage(img);
    SetTextureFilter(page->texture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    page->shelf_x = GLYPH_PADDING;
    page->shelf_y = GLYPH_PADDING;
    page->shelf_h = 0;
    return page;
}

static bool atlas_reserve(FontCache *cache, i32 w, i32 h, i32 *page_index, i32 *x, i32 *y) {
    if (w + 2 * GLYPH_PADDING > PAGE_SIZE || h + 2 * GLYPH_PADDING > PAGE_SIZE) return false;
    AtlasPage *page = cache->page_count ? &cache->pages[cache->page_count - 1] : page_new(cache);
    if (!page) return false;
    if (page->shelf_x + w + GLYPH_PADDING > PAGE_SIZE) {
        page->shelf_x = GLYPH_PADDING;
        page->shelf_y += page->shelf_h + GLYPH_PADDING;
        page->shelf_h = 0;
    }
    if (page->shelf_y + h + GLYPH_PADDING > PAGE_SIZE) {
        page = page_new(cache);
        if (!page) return false;
    }
    *page_index = (i32)(page - cache->pages);
    *x = page->shelf_x;
    *y = page->shelf_y;
    page->shelf_x += w + GLYPH_PADDING;
    page->shelf_h = MAX(page->shelf_h, h);
    return true;
}

// ---- Glyph resolution ------------------------------------------------------------------------

static const Glyph *glyph_get(FontCache *cache, FontRole role, f32 size, u32 cp) {
    if (!cache->slot_count) table_grow(cache);
    u64 key = glyph_key(role, cp, size);
    Glyph *found = table_find(cache, key);
    if (found) return found;

    Glyph g = { .key = key, .page = -1 };
    // Find the first face in the chain that has this codepoint.
    FontFace *face = NULL;
    int glyph_index = 0;
    for (i32 i = 0; i < cache->chain_len[role]; ++i) {
        FontFace *f = &cache->faces[cache->chains[role][i]];
        if (!face_ensure_loaded(f)) continue;
        glyph_index = stbtt_FindGlyphIndex(&f->info, (int)cp);
        if (glyph_index) {
            face = f;
            break;
        }
    }
    if (!face) {
        // Tofu: an empty advance box, drawn as an outline by font_draw.
        g.missing = cp > 0x20;
        g.advance = size * 0.5f;
        return table_insert(cache, g);
    }

    f32 scale = face_scale(face, size);
    int advance = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&face->info, glyph_index, &advance, &lsb);
    g.advance = (f32)advance * scale;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&face->info, glyph_index, scale, scale, &x0, &y0, &x1, &y1);
    i32 w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        isize needed = (isize)w * h;
        if (needed > cache->scratch_size) {
            free(cache->scratch);
            cache->scratch = malloc((usize)needed * 3);
            if (!cache->scratch) abort();
            cache->scratch_size = needed;
        }
        u8 *alpha = cache->scratch;
        stbtt_MakeGlyphBitmap(&face->info, alpha, w, h, w, scale, scale, glyph_index);
        i32 page, ax, ay;
        if (atlas_reserve(cache, w, h, &page, &ax, &ay)) {
            // Expand to gray+alpha (white glyph, coverage in alpha) for tinting.
            u8 *ga = alpha + needed;
            for (isize i = 0; i < needed; ++i) {
                ga[i * 2] = 255;
                ga[i * 2 + 1] = alpha[i];
            }
            UpdateTextureRec(cache->pages[page].texture, (Rectangle){ (f32)ax, (f32)ay, (f32)w, (f32)h }, ga);
            g.page = (i16)page;
            g.x = (u16)ax;
            g.y = (u16)ay;
            g.w = (u16)w;
            g.h = (u16)h;
            g.x_offset = (f32)x0;
            g.y_offset = (f32)y0;
        }
    }
    return table_insert(cache, g);
}

// ---- Public API ---------------------------------------------------------------------------------

FontCache *font_cache_create(void) {
    FontCache *cache = calloc(1, sizeof(FontCache));
    if (!cache) abort();
    return cache;
}

void font_cache_clear_glyphs(FontCache *cache) {
    for (i32 i = 0; i < cache->page_count; ++i) UnloadTexture(cache->pages[i].texture);
    cache->page_count = 0;
    free(cache->slots);
    cache->slots = NULL;
    cache->slot_count = 0;
    cache->glyph_count = 0;
}

void font_cache_destroy(FontCache *cache) {
    if (!cache) return;
    font_cache_clear_glyphs(cache);
    for (i32 i = 0; i < cache->face_count; ++i) {
        string_free(&cache->faces[i].path);
        string_free(&cache->faces[i].data);
    }
    free(cache->scratch);
    free(cache);
}

i32 font_cache_add_face(FontCache *cache, StringView path, bool lazy) {
    for (i32 i = 0; i < cache->face_count; ++i) {
        if (sv_eq(string_sv(&cache->faces[i].path), path)) return i;
    }
    if (cache->face_count >= MAX_FACES) return -1;
    i32 id = cache->face_count++;
    cache->faces[id] = (FontFace){ .path = string_from(path) };
    if (!lazy && !face_ensure_loaded(&cache->faces[id])) return -1;
    return id;
}

void font_cache_set_chain(FontCache *cache, FontRole role, const i32 *faces, i32 count) {
    cache->chain_len[role] = 0;
    for (i32 i = 0; i < count && cache->chain_len[role] < MAX_CHAIN; ++i) {
        if (faces[i] >= 0) cache->chains[role][cache->chain_len[role]++] = faces[i];
    }
    font_cache_clear_glyphs(cache);
}

static FontFace *primary_face(FontCache *cache, FontRole role) {
    for (i32 i = 0; i < cache->chain_len[role]; ++i) {
        FontFace *f = &cache->faces[cache->chains[role][i]];
        if (face_ensure_loaded(f)) return f;
    }
    return NULL;
}

f32 font_line_height(FontCache *cache, FontRole role, f32 size) {
    FontFace *f = primary_face(cache, role);
    if (!f) return size * 1.3f;
    return (f32)(f->ascent - f->descent + f->line_gap) * face_scale(f, size);
}

static f32 baseline_offset(FontCache *cache, FontRole role, f32 size) {
    FontFace *f = primary_face(cache, role);
    if (!f) return size;
    f32 line = (f32)(f->ascent - f->descent + f->line_gap) * face_scale(f, size);
    f32 content = (f32)(f->ascent - f->descent) * face_scale(f, size);
    return (line - content) * 0.5f + (f32)f->ascent * face_scale(f, size);
}

Vector2 font_measure(FontCache *cache, FontRole role, f32 size, StringView text) {
    f32 width = 0, max_width = 0;
    i32 lines = 1;
    while (text.len > 0) {
        u32 cp = utf8_next(&text);
        if (cp == '\n') {
            max_width = MAX(max_width, width);
            width = 0;
            lines++;
            continue;
        }
        width += glyph_get(cache, role, size, cp)->advance;
    }
    return (Vector2){ MAX(max_width, width), font_line_height(cache, role, size) * (f32)lines };
}

void font_draw(FontCache *cache, FontRole role, f32 size, StringView text, Vector2 pos, Color color) {
    f32 line_height = font_line_height(cache, role, size);
    f32 baseline = floorf(pos.y + baseline_offset(cache, role, size) + 0.5f);
    f32 pen = pos.x;
    while (text.len > 0) {
        u32 cp = utf8_next(&text);
        if (cp == '\n') {
            pen = pos.x;
            baseline += line_height;
            continue;
        }
        const Glyph *g = glyph_get(cache, role, size, cp);
        if (g->page >= 0) {
            Rectangle src = { (f32)g->x, (f32)g->y, (f32)g->w, (f32)g->h };
            Rectangle dst = { floorf(pen + g->x_offset + 0.5f), baseline + g->y_offset, (f32)g->w, (f32)g->h };
            DrawTexturePro(cache->pages[g->page].texture, src, dst, (Vector2){ 0, 0 }, 0.0f, color);
        } else if (g->missing) {
            DrawRectangleLinesEx((Rectangle){ pen + 1, baseline - size * 0.7f, g->advance - 2, size * 0.75f }, 1.0f,
                                 Fade(color, 0.5f));
        }
        pen += g->advance;
    }
}

void font_draw_centered(FontCache *cache, FontRole role, f32 size, StringView text, Vector2 center, Color color) {
    Vector2 dim = font_measure(cache, role, size, text);
    font_draw(cache, role, size, text, (Vector2){ floorf(center.x - dim.x * 0.5f + 0.5f), floorf(center.y - dim.y * 0.5f + 0.5f) }, color);
}
