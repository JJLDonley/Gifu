#include "ui/background.h"

#include "base/log.h"

#include <math.h>
#include <stdlib.h>

// ---- Noise ------------------------------------------------------------------------------

static u32 hash2(i32 x, i32 y, u32 seed) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static f32 rand01(i32 x, i32 y, u32 seed) { return (f32)(hash2(x, y, seed) & 0xFFFFFF) / (f32)0xFFFFFF; }

static f32 smooth(f32 t) { return t * t * (3.0f - 2.0f * t); }

static f32 value_noise(f32 x, f32 y, u32 seed) {
    i32 xi = (i32)floorf(x), yi = (i32)floorf(y);
    f32 tx = smooth(x - (f32)xi), ty = smooth(y - (f32)yi);
    f32 a = rand01(xi, yi, seed), b = rand01(xi + 1, yi, seed);
    f32 c = rand01(xi, yi + 1, seed), d = rand01(xi + 1, yi + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

static f32 fbm(f32 x, f32 y, u32 seed, i32 octaves) {
    f32 sum = 0, amp = 0.5f, norm = 0;
    for (i32 i = 0; i < octaves; ++i) {
        sum += value_noise(x, y, seed + (u32)i * 101u) * amp;
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// ---- Coffee stains --------------------------------------------------------------------------

typedef struct Stain {
    f32 cx, cy, radius, thickness, strength;
    f32 arc_start, arc_len;   // partial rings
    u32 seed;
    bool drop;                // small filled splash instead of a ring
} Stain;

static f32 stain_intensity(const Stain *s, f32 x, f32 y) {
    f32 dx = x - s->cx, dy = y - s->cy;
    f32 r = sqrtf(dx * dx + dy * dy);
    if (r > s->radius * 1.25f) return 0.0f;
    f32 angle = atan2f(dy, dx);
    // Wobbly edge: radius modulated by low-frequency noise around the circle.
    f32 a01 = (angle + PI) / (2.0f * PI);
    f32 wobble = fbm(a01 * 6.0f, 0.5f, s->seed, 3) - 0.5f;
    f32 edge = s->radius * (1.0f + 0.10f * wobble);
    f32 d = r - edge;
    if (s->drop) return d < 0 ? s->strength * (0.55f + 0.45f * expf(d / s->thickness)) : 0.0f;

    f32 ring = d <= 0 ? 0.10f + 0.90f * expf(d / s->thickness) : expf(-d / 0.9f);
    // Partial arcs: fade the ring outside [arc_start, arc_start + arc_len].
    f32 rel = fmodf(a01 - s->arc_start + 2.0f, 1.0f);
    f32 arc = rel < s->arc_len ? 1.0f : 0.0f;
    f32 soft = fminf(1.0f, fminf(rel, fabsf(s->arc_len - rel)) * 12.0f);
    f32 fill = d <= 0 ? 0.08f : 0.0f;
    return s->strength * (fill + ring * arc * soft * 0.9f);
}

Image paper_generate(i32 width, i32 height, Color base, const PaperParams *params, bool with_stains) {
    Image img = GenImageColor(width, height, base);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color *px = img.data;
    u32 seed = params->seed * 7919u + 17u;

    Stain stains[16];
    i32 stain_count = with_stains ? MIN(params->stains, (i32)countof(stains)) : 0;
    for (i32 i = 0; i < stain_count; ++i) {
        Stain *s = &stains[i];
        s->seed = seed + (u32)i * 31u;
        s->drop = i >= 3 && rand01(i, 7, seed) < 0.5f;
        s->cx = (0.08f + 0.84f * rand01(i, 1, seed)) * (f32)width;
        s->cy = (0.08f + 0.84f * rand01(i, 2, seed)) * (f32)height;
        f32 scale = (f32)MIN(width, height);
        s->radius = s->drop ? scale * (0.008f + 0.02f * rand01(i, 3, seed)) : scale * (0.09f + 0.10f * rand01(i, 3, seed));
        s->thickness = s->drop ? 2.0f : 2.5f + 4.0f * rand01(i, 4, seed);
        s->strength = s->drop ? 0.25f + 0.2f * rand01(i, 8, seed) : 0.35f + 0.35f * rand01(i, 5, seed);
        s->arc_start = rand01(i, 6, seed);
        s->arc_len = 0.55f + 0.45f * rand01(i, 9, seed);
    }

    f32 cx = (f32)width * 0.5f, cy = (f32)height * 0.5f;
    f32 max_d = sqrtf(cx * cx + cy * cy);
    for (i32 y = 0; y < height; ++y) {
        for (i32 x = 0; x < width; ++x) {
            f32 fx = (f32)x, fy = (f32)y;
            // Low-frequency mottling + per-pixel grain.
            f32 mottle = (fbm(fx / 220.0f, fy / 220.0f, seed, 4) - 0.5f) * params->grain * 2.2f;
            f32 grain = (rand01(x, y, seed ^ 0xA5A5u) - 0.5f) * params->grain;
            // Fibers: noise stretched along x.
            f32 fiber = 0;
            if (params->fibers > 0) {
                f32 n = fbm(fx / 90.0f, fy / 3.0f, seed + 999u, 2);
                fiber = (n > 0.62f ? (n - 0.62f) * 2.5f : 0.0f) * params->fibers * 0.25f;
            }
            f32 dx = fx - cx, dy = fy - cy;
            f32 d = sqrtf(dx * dx + dy * dy) / max_d;
            f32 vignette = params->vignette * smooth(CLAMP((d - 0.45f) / 0.55f, 0.0f, 1.0f)) * 0.5f;
            f32 lum = 1.0f + mottle + grain - fiber - vignette;

            f32 r = base.r * lum, g = base.g * lum, b = base.b * lum;
            for (i32 i = 0; i < stain_count; ++i) {
                f32 k = stain_intensity(&stains[i], fx, fy);
                if (k <= 0.001f) continue;
                // Multiply blend towards the stain colour (coffee darkens paper).
                f32 sr = params->stain_color.r / 255.0f, sg = params->stain_color.g / 255.0f, sb = params->stain_color.b / 255.0f;
                r *= 1.0f - k + k * sr;
                g *= 1.0f - k + k * sg;
                b *= 1.0f - k + k * sb;
            }
            Color *p = &px[y * width + x];
            p->r = (u8)CLAMP(r, 0.0f, 255.0f);
            p->g = (u8)CLAMP(g, 0.0f, 255.0f);
            p->b = (u8)CLAMP(b, 0.0f, 255.0f);
            p->a = 255;
        }
    }
    return img;
}

// ---- Background -------------------------------------------------------------------------------

static void unload(Texture2D *t) {
    if (t->id) UnloadTexture(*t);
    *t = (Texture2D){ 0 };
}

enum { PAPER_VERSION = 1 };   // bump when paper_generate's algorithm changes

static u64 paper_key(Color base, const PaperParams *p, i32 w, i32 h, bool stains) {
    u64 key = 1469598103934665603ull;
    const u8 *parts[] = { (const u8 *)&base, (const u8 *)&p->grain, (const u8 *)&p->fibers, (const u8 *)&p->vignette,
                          (const u8 *)&p->stains, (const u8 *)&p->stain_color, (const u8 *)&p->seed };
    const usize sizes[] = { sizeof(base), sizeof(p->grain), sizeof(p->fibers), sizeof(p->vignette), sizeof(p->stains),
                            sizeof(p->stain_color), sizeof(p->seed) };
    for (i32 i = 0; i < (i32)countof(parts); ++i) {
        for (usize k = 0; k < sizes[i]; ++k) key = (key ^ parts[i][k]) * 1099511628211ull;
    }
    i64 extra[] = { w, h, stains, PAPER_VERSION };
    for (i32 i = 0; i < (i32)countof(extra); ++i) key = (key ^ (u64)extra[i]) * 1099511628211ull;
    return key;
}

static i32 paper_worker(void *arg) {
    PaperJob *job = arg;
    Image img = paper_generate(job->w, job->h, job->base, &job->params, job->stains);
    if (job->cache_path.len && !ExportImage(img, string_cstr(&job->cache_path))) log_warn("paper cache: cannot write %s", string_cstr(&job->cache_path));
    mutex_lock(&job->lock);
    job->image = img;
    job->done = true;
    mutex_unlock(&job->lock);
    return 0;
}

static void job_finish(PaperJob *job) {   // joins the worker; frees what it made
    if (!job->running) return;
    thread_join(&job->thread);
    mutex_destroy(&job->lock);
    if (job->image.data) UnloadImage(job->image);
    job->image = (Image){ 0 };
    string_free(&job->cache_path);
    job->running = job->done = false;
}

// Cached PNG: loaded now (fast). Otherwise generated on a worker thread; the
// solid theme colour shows until background_poll uploads it.
static void paper_texture(Background *bg, StringView cache_dir, i32 w, i32 h, Color base, const PaperParams *p, bool stains, bool for_board) {
    String file = string_format("paper-%016llx.png", (unsigned long long)paper_key(base, p, w, h, stains));
    String path = path_join(cache_dir, string_sv(&file));
    string_free(&file);
    Texture2D tex = { 0 };
    if (os_file_exists(string_sv(&path))) tex = LoadTexture(string_cstr(&path));
    if (tex.id) {
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        *(for_board ? &bg->paper : &bg->texture) = tex;
        string_free(&path);
        return;
    }
    PaperJob *job = &bg->jobs[for_board ? 1 : 0];
    job_finish(job);   // an older job for the same slot (its result is stale anyway)
    *job = (PaperJob){ .for_board = for_board, .generation = bg->generation, .w = w, .h = h, .base = base, .params = *p, .stains = stains };
    if (cache_dir.len && os_make_dirs(cache_dir)) job->cache_path = path;
    else string_free(&path);
    mutex_init(&job->lock);
    job->running = true;
    if (!thread_start(&job->thread, paper_worker, job)) {   // no thread: do it here
        job->running = false;
        mutex_destroy(&job->lock);
        Image img = paper_generate(w, h, base, p, stains);
        tex = LoadTextureFromImage(img);
        UnloadImage(img);
        string_free(&job->cache_path);
        if (tex.id) SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        *(for_board ? &bg->paper : &bg->texture) = tex;
    }
}

bool background_poll(Background *bg) {
    bool appeared = false;
    for (i32 i = 0; i < 2; ++i) {
        PaperJob *job = &bg->jobs[i];
        if (!job->running) continue;
        mutex_lock(&job->lock);
        bool done = job->done;
        mutex_unlock(&job->lock);
        if (!done) continue;
        if (job->generation == bg->generation && job->image.data) {
            Texture2D tex = LoadTextureFromImage(job->image);
            if (tex.id) {
                SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                Texture2D *slot = job->for_board ? &bg->paper : &bg->texture;
                unload(slot);
                *slot = tex;
                appeared = true;
            }
        }
        job_finish(job);
    }
    return appeared;
}

void background_build(Background *bg, const Theme *theme, StringView assets_dir, StringView cache_dir) {
    if (sv_eq(string_sv(&bg->built_for), string_sv(&theme->id))) return;
    unload(&bg->texture);
    unload(&bg->paper);
    string_free(&bg->built_for);
    bg->built_for = string_from(string_sv(&theme->id));
    bg->generation++;   // results of jobs started for an earlier theme are dropped

    if (theme->background.kind == BACKGROUND_IMAGE && theme->background.image.len) {
        StringView image = string_sv(&theme->background.image);
        String path = os_file_exists(image) ? string_from(image) : path_join(assets_dir, image);
        bg->texture = LoadTexture(string_cstr(&path));
        if (bg->texture.id) SetTextureFilter(bg->texture, TEXTURE_FILTER_BILINEAR);
        else log_warn("theme %s: cannot load background %s", string_cstr(&theme->id), string_cstr(&path));
        string_free(&path);
    }
    bool want_paper = theme->background.kind == BACKGROUND_PAPER || theme->board.style == BOARD_STYLE_PAPER;
    if (want_paper) {
        f64 start = GetTime();
        const PaperParams *p = &theme->background.paper;
        if (theme->background.kind == BACKGROUND_PAPER) paper_texture(bg, cache_dir, 1600, 1000, theme->colors.background, p, true, false);
        if (theme->board.style == BOARD_STYLE_PAPER) paper_texture(bg, cache_dir, 800, 800, theme->colors.board, p, false, true);
        bool generating = bg->jobs[0].running || bg->jobs[1].running;
        log_info("theme %s: paper %s in %.0f ms", string_cstr(&theme->id), generating ? "generating in the background, started" : "ready",
                 (GetTime() - start) * 1000.0);
    }
}

static Rectangle cover_source(Texture2D t, Rectangle area) {
    f32 tex_aspect = (f32)t.width / (f32)t.height;
    f32 area_aspect = area.width / MAX(area.height, 1.0f);
    if (area_aspect > tex_aspect) {
        f32 h = (f32)t.width / area_aspect;
        return (Rectangle){ 0, ((f32)t.height - h) * 0.5f, (f32)t.width, h };
    }
    f32 w = (f32)t.height * area_aspect;
    return (Rectangle){ ((f32)t.width - w) * 0.5f, 0, w, (f32)t.height };
}

void background_draw(const Background *bg, const Theme *theme, Rectangle area) {
    DrawRectangleRec(area, theme->colors.background);
    if (!bg->texture.id) return;
    Color tint = color_with_alpha(WHITE, theme->background.kind == BACKGROUND_IMAGE ? theme->background.opacity : 1.0f);
    if (theme->background.kind == BACKGROUND_IMAGE && theme->background.fit == BACKGROUND_FIT_TILE) {
        for (f32 y = area.y; y < area.y + area.height; y += (f32)bg->texture.height) {
            for (f32 x = area.x; x < area.x + area.width; x += (f32)bg->texture.width) DrawTexture(bg->texture, (i32)x, (i32)y, tint);
        }
        return;
    }
    Rectangle src = theme->background.fit == BACKGROUND_FIT_STRETCH && theme->background.kind == BACKGROUND_IMAGE
                        ? (Rectangle){ 0, 0, (f32)bg->texture.width, (f32)bg->texture.height }
                        : cover_source(bg->texture, area);
    DrawTexturePro(bg->texture, src, area, (Vector2){ 0, 0 }, 0, tint);
}

void background_free(Background *bg) {
    for (i32 i = 0; i < 2; ++i) job_finish(&bg->jobs[i]);
    unload(&bg->texture);
    unload(&bg->paper);
    string_free(&bg->built_for);
}
