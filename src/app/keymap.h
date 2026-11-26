// keymap.h - key chords ("Ctrl+Shift+O", "Right", "Space") bound to command ids,
// loaded from key=value files: assets/data/keymap.kv, then the user's
// <config_dir>/keymap.kv (one "Chord = command.id" per line).
// Key codes are raylib/GLFW key codes (letters/digits are their ASCII values).
#pragma once

#include "base/kv.h"
#include "base/vec.h"

// Mouse buttons bind like keys (user feedback 2026-11-25): code KEY_MOUSE_BASE + raylib mouse button, named
// MouseMiddle, Mouse4 (back thumb button), Mouse5 (forward), Mouse6, Mouse7 — e.g. "Mouse4 = nav.back".
// Left and right buttons stay with the board (play, menus) and cannot be bound.
enum { KEY_MOUSE_BASE = 1000 };
static inline bool key_chord_is_mouse(i32 key) { return key >= KEY_MOUSE_BASE + 2 && key <= KEY_MOUSE_BASE + 6; }

enum {
    KEYMOD_CTRL = 1 << 0,
    KEYMOD_SHIFT = 1 << 1,
    KEYMOD_ALT = 1 << 2,
    KEYMOD_SUPER = 1 << 3,
};

typedef struct KeyChord {
    i32 key;
    u8 mods;
} KeyChord;

typedef struct KeyBinding {
    KeyChord chord;
    String command;
} KeyBinding;

typedef struct Keymap {
    Vec(KeyBinding) bindings;
} Keymap;

NODISCARD bool key_chord_parse(StringView text, KeyChord *out);
// "Ctrl+O" style text for tooltips. Returns bytes written.
i32 key_chord_format(KeyChord chord, char *buffer, i32 capacity);

// Adds "Chord = command" entries; a later binding for the same chord wins, and
// "Chord = none" removes the chord (a user file unbinding a default key).
// `source` names the file in warnings.
void keymap_load_kv(Keymap *keymap, const KvFile *kv, StringView source);
// Binds `chord` to `command` (taking it from any other command).
void keymap_set(Keymap *keymap, KeyChord chord, StringView command);
void keymap_remove_chord(Keymap *keymap, KeyChord chord);
void keymap_clear_command(Keymap *keymap, StringView command);
// The user file that turns `defaults` into `keymap`: changed chords, "none" for removed ones.
String keymap_diff_kv(const Keymap *keymap, const Keymap *defaults);
const KeyBinding *keymap_lookup(const Keymap *keymap, KeyChord chord);
const KeyBinding *keymap_find_command(const Keymap *keymap, StringView command);
void keymap_free(Keymap *keymap);
