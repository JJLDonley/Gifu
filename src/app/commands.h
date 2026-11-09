// commands.h - named, data-addressable actions.
//
// Every user action ("nav.next", "analysis.toggle", "theme.next", ...) is a
// CommandDef registered once. Keymaps, toolbars and menus refer to commands by
// id in JSON, so adding a binding or a button never touches code.
#pragma once

#include "base/str.h"
#include "base/vec.h"

typedef void (*CommandRun)(void *context);
typedef bool (*CommandQuery)(const void *context);

typedef struct CommandDef {
    const char *id;
    const char *label;     // default (English) label; i18n key is "command.<id>"
    const char *icon;      // short glyph/text used by icon buttons
    CommandRun run;
    CommandQuery enabled;  // NULL = always enabled
    CommandQuery checked;  // NULL = not a toggle
    void *context;         // NULL = the registry's context (the App)
} CommandDef;

typedef struct CommandRegistry {
    Vec(CommandDef) commands;
    void *context;
} CommandRegistry;

void command_registry_init(CommandRegistry *registry, void *context);
void command_registry_free(CommandRegistry *registry);
void command_register(CommandRegistry *registry, CommandDef def);
void command_register_all(CommandRegistry *registry, const CommandDef *defs, isize count);
const CommandDef *command_find(const CommandRegistry *registry, StringView id);
bool command_is_enabled(const CommandRegistry *registry, const CommandDef *cmd);
bool command_is_checked(const CommandRegistry *registry, const CommandDef *cmd);
// Runs the command if it exists and is enabled; returns whether it ran.
bool command_execute(CommandRegistry *registry, StringView id);
