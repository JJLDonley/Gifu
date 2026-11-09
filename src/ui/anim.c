#include "ui/anim.h"

#include <math.h>

void anim_begin_frame(AnimStore *store, f32 dt) {
    store->frame++;
    store->dt = CLAMP(dt, 0.0f, 1.0f / 20.0f);   // avoid jumps after stalls
    store->moved = false;
}

static u32 mix(u32 k) {
    k ^= k >> 16;
    k *= 0x7feb352du;
    k ^= k >> 15;
    return k;
}

f32 anim_towards(AnimStore *store, u32 key, f32 target, f32 rate, f32 initial) {
    if (key == 0) key = 1;
    u32 index = mix(key) & (ANIM_SLOTS - 1);
    AnimSlot *slot = NULL;
    // Linear probe a few slots; reuse stale ones (unused for > 2 s at 60 fps).
    for (u32 i = 0; i < 8; ++i) {
        AnimSlot *s = &store->slots[(index + i) & (ANIM_SLOTS - 1)];
        if (s->key == key) {
            slot = s;
            break;
        }
        if (!slot && (s->key == 0 || store->frame - s->last_frame > 120)) slot = s;
    }
    if (!slot) slot = &store->slots[index];
    if (slot->key != key) {
        slot->key = key;
        slot->value = initial;
    }
    slot->last_frame = store->frame;
    f32 k = 1.0f - expf(-rate * store->dt);
    f32 before = slot->value;
    slot->value += (target - slot->value) * k;
    if (fabsf(slot->value - target) < 0.001f) slot->value = target;
    if (fabsf(slot->value - before) > 0.0005f) store->moved = true;
    return slot->value;
}

void anim_set(AnimStore *store, u32 key, f32 value) {
    anim_towards(store, key, value, 0.0f, value);   // ensure the slot exists
    u32 index = mix(key ? key : 1) & (ANIM_SLOTS - 1);
    for (u32 i = 0; i < 8; ++i) {
        AnimSlot *s = &store->slots[(index + i) & (ANIM_SLOTS - 1)];
        if (s->key == (key ? key : 1)) {
            s->value = value;
            return;
        }
    }
}

bool anim_active(const AnimStore *store) { return store->moved; }
