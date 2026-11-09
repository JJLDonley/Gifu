#include "app/keymap.h"

#include "base/log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Subset of raylib KeyboardKey values (identical to GLFW key codes).
static const struct {
    const char *name;
    i32 code;
} KEY_NAMES[] = {
    { "Space", 32 },     { "Apostrophe", 39 }, { "Comma", 44 },      { "Minus", 45 },     { "Period", 46 },
    { "Slash", 47 },     { "Semicolon", 59 },  { "Equal", 61 },      { "LeftBracket", 91 }, { "Backslash", 92 },
    { "RightBracket", 93 }, { "Grave", 96 },   { "Escape", 256 },    { "Enter", 257 },    { "Tab", 258 },
    { "Backspace", 259 }, { "Insert", 260 },   { "Delete", 261 },    { "Right", 262 },    { "Left", 263 },
    { "Down", 264 },     { "Up", 265 },        { "PageUp", 266 },    { "PageDown", 267 }, { "Home", 268 },
    { "End", 269 },      { "F1", 290 },        { "F2", 291 },        { "F3", 292 },       { "F4", 293 },
    { "F5", 294 },       { "F6", 295 },        { "F7", 296 },        { "F8", 297 },       { "F9", 298 },
    { "F10", 299 },      { "F11", 300 },       { "F12", 301 },
};

bool key_chord_parse(StringView text, KeyChord *out) {
    KeyChord chord = { 0 };
    StringView rest = sv_trim(text);
    while (rest.len > 0) {
        StringView part = sv_trim(sv_chop_char(&rest, '+'));
        if (part.len == 0) return false;
        if (sv_eq_nocase(part, SV("Ctrl")) || sv_eq_nocase(part, SV("Control"))) chord.mods |= KEYMOD_CTRL;
        else if (sv_eq_nocase(part, SV("Shift"))) chord.mods |= KEYMOD_SHIFT;
        else if (sv_eq_nocase(part, SV("Alt"))) chord.mods |= KEYMOD_ALT;
        else if (sv_eq_nocase(part, SV("Super")) || sv_eq_nocase(part, SV("Cmd"))) chord.mods |= KEYMOD_SUPER;
        else if (part.len == 1 && isalnum((u8)part.data[0])) chord.key = toupper((u8)part.data[0]);
        else {
            bool found = false;
            for (i32 i = 0; i < (i32)countof(KEY_NAMES) && !found; ++i) {
                if (sv_eq_nocase(part, sv(KEY_NAMES[i].name))) chord.key = KEY_NAMES[i].code, found = true;
            }
            if (!found) return false;
        }
    }
    if (chord.key == 0) return false;
    *out = chord;
    return true;
}

i32 key_chord_format(KeyChord chord, char *buffer, i32 capacity) {
    const char *key_name = NULL;
    char single[2] = { 0 };
    for (i32 i = 0; i < (i32)countof(KEY_NAMES); ++i) {
        if (KEY_NAMES[i].code == chord.key) key_name = KEY_NAMES[i].name;
    }
    if (!key_name) {
        single[0] = (char)chord.key;
        key_name = single;
    }
    return snprintf(buffer, (usize)capacity, "%s%s%s%s%s", chord.mods & KEYMOD_CTRL ? "Ctrl+" : "",
                    chord.mods & KEYMOD_SHIFT ? "Shift+" : "", chord.mods & KEYMOD_ALT ? "Alt+" : "",
                    chord.mods & KEYMOD_SUPER ? "Super+" : "", key_name);
}

void keymap_load_kv(Keymap *keymap, const KvFile *kv, StringView source) {
    vec_foreach(KvEntry, e, &kv->entries) {
        KeyChord chord;
        if (!key_chord_parse(e->key, &chord) || e->value.len == 0) {
            log_warn(SV_FMT ":%d: bad key binding '" SV_FMT "'", SV_ARG(source), e->line, SV_ARG(e->key));
            continue;
        }
        bool replaced = false;
        vec_foreach(KeyBinding, existing, &keymap->bindings) {
            if (existing->chord.key == chord.key && existing->chord.mods == chord.mods) {
                string_free(&existing->command);
                existing->command = string_from(e->value);
                replaced = true;
            }
        }
        if (!replaced) vec_push(&keymap->bindings, (KeyBinding){ chord, string_from(e->value) });
    }
}

const KeyBinding *keymap_lookup(const Keymap *keymap, KeyChord chord) {
    vec_foreach(KeyBinding, b, &keymap->bindings) {
        if (b->chord.key == chord.key && b->chord.mods == chord.mods) return b;
    }
    return NULL;
}

const KeyBinding *keymap_find_command(const Keymap *keymap, StringView command) {
    vec_foreach(KeyBinding, b, &keymap->bindings) {
        if (sv_eq(string_sv(&b->command), command)) return b;
    }
    return NULL;
}

void keymap_free(Keymap *keymap) {
    vec_foreach(KeyBinding, b, &keymap->bindings) string_free(&b->command);
    vec_free(&keymap->bindings);
}
