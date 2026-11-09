// anim.h - frame-rate independent animation helpers.
//
// AnimStore keeps one eased value per key (usually a Clay element id, or a
// hash of a board point). Widgets ask for "the value that is moving towards
// `target`" every frame; no widget needs to own animation state.
#pragma once

#include "base/base.h"

enum { ANIM_SLOTS = 2048 };

typedef struct AnimSlot {
    u32 key;
    u32 last_frame;
    f32 value;
} AnimSlot;

typedef struct AnimStore {
    AnimSlot slots[ANIM_SLOTS];
    u32 frame;
    f32 dt;      // seconds since the previous frame (clamped)
    bool moved;  // some value changed noticeably this frame
} AnimStore;

void anim_begin_frame(AnimStore *store, f32 dt);
// Exponential approach: `rate` ~ 1/seconds (12 = settles in ~0.25 s).
// A key seen for the first time starts at `initial`.
f32 anim_towards(AnimStore *store, u32 key, f32 target, f32 rate, f32 initial);
void anim_set(AnimStore *store, u32 key, f32 value);
// True while any value moved noticeably this frame (keeps the UI redrawing).
bool anim_active(const AnimStore *store);

// Easing curves on t in [0,1].
static inline f32 ease_out_cubic(f32 t) {
    t = CLAMP(t, 0.0f, 1.0f);
    f32 u = 1.0f - t;
    return 1.0f - u * u * u;
}
static inline f32 ease_out_back(f32 t) {
    t = CLAMP(t, 0.0f, 1.0f);
    const f32 c1 = 1.70158f, c3 = c1 + 1.0f;
    f32 u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
static inline f32 ease_in_out(f32 t) {
    t = CLAMP(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
