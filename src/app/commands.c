#include "app/commands.h"

#include "base/log.h"

void command_registry_init(CommandRegistry *registry, void *context) {
    *registry = (CommandRegistry){ .context = context };
}

void command_registry_free(CommandRegistry *registry) { vec_free(&registry->commands); }

void command_register(CommandRegistry *registry, CommandDef def) {
    ASSERT(def.id && def.run);
    if (command_find(registry, sv(def.id))) {
        log_warn("command '%s' registered twice", def.id);
        return;
    }
    vec_push(&registry->commands, def);
}

void command_register_all(CommandRegistry *registry, const CommandDef *defs, isize count) {
    for (isize i = 0; i < count; ++i) command_register(registry, defs[i]);
}

const CommandDef *command_find(const CommandRegistry *registry, StringView id) {
    vec_foreach(CommandDef, c, &registry->commands) {
        if (sv_eq(sv(c->id), id)) return c;
    }
    return NULL;
}

static void *context_for(const CommandRegistry *registry, const CommandDef *cmd) {
    return cmd->context ? cmd->context : registry->context;
}

bool command_is_enabled(const CommandRegistry *registry, const CommandDef *cmd) {
    return cmd && (!cmd->enabled || cmd->enabled(context_for(registry, cmd)));
}

bool command_is_checked(const CommandRegistry *registry, const CommandDef *cmd) {
    return cmd && cmd->checked && cmd->checked(context_for(registry, cmd));
}

bool command_execute(CommandRegistry *registry, StringView id) {
    const CommandDef *cmd = command_find(registry, id);
    if (!cmd) {
        log_warn("unknown command '" SV_FMT "'", SV_ARG(id));
        return false;
    }
    if (!command_is_enabled(registry, cmd)) return false;
    cmd->run(context_for(registry, cmd));
    return true;
}
