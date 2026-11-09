// keymap.h - key chords ("Ctrl+Shift+O", "Right", "Space") bound to command ids,
// loaded from key=value files: assets/data/keymap.kv, then the user's
// <config_dir>/keymap.kv (one "Chord = command.id" per line).
// Key codes are raylib/GLFW key codes (letters/digits are their ASCII values).
#pragma once

#include "base/kv.h"
#include "base/vec.h"

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

// Adds "Chord = command" entries; a later binding for the same chord wins.
// `source` names the file in warnings.
void keymap_load_kv(Keymap *keymap, const KvFile *kv, StringView source);
const KeyBinding *keymap_lookup(const Keymap *keymap, KeyChord chord);
const KeyBinding *keymap_find_command(const Keymap *keymap, StringView command);
void keymap_free(Keymap *keymap);
