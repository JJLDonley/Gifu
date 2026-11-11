// dbus_link.h - a D-Bus connection inside the process (Linux), for the desktop
// services Gifu talks to: KWin's ScreenShot2 and the xdg-desktop-portal
// Screenshot / ScreenCast portals (platform_linux.c).
//
// libdbus-1 is loaded at run time (dlopen "libdbus-1.so.3"), so the program
// still starts where it is missing; dbus_link_available() says whether it is
// there. One link is used by one thread at a time.
//
// Portal answers (org.freedesktop.portal.Request.Response) are sent only to the
// connection that made the request, and a request is cancelled when that
// connection goes away: the call and the wait must use the same link
// (dbus_link_wait_signal), with the match added before the call.
#pragma once

#include "base/str.h"

bool dbus_link_available(void);

typedef struct DbusLink DbusLink;

DbusLink *dbus_link_open_session(String *error);   // a private session-bus connection
void dbus_link_close(DbusLink *link);
StringView dbus_link_unique_name(const DbusLink *link);   // ":1.42"
bool dbus_link_add_match(DbusLink *link, StringView rule, String *error);
bool dbus_link_request_name(DbusLink *link, StringView name, String *error);   // tests: be a service

// ---- Values (replies, signal arguments) ------------------------------------------------------

typedef struct DbusValue DbusValue;
struct DbusValue {
    char type;          // D-Bus type code: 's' 'o' 'g' 'b' 'y' 'n' 'q' 'i' 'u' 'x' 't' 'd' 'h' 'v' 'a' 'r' 'e'
    i64 i;              // integers, booleans, bytes
    f64 d;
    String s;           // strings, object paths, signatures
    i32 fd;             // 'h' (owned: dbus_value_free closes it)
    DbusValue *items;   // 'a' elements, 'r' fields, 'e' key + value, 'v' the value
    isize count;
};

void dbus_value_free(DbusValue *v);
// In an a{sv} dictionary: the value under `key` (variants unwrapped), or NULL.
const DbusValue *dbus_dict_get(const DbusValue *dict, StringView key);
static inline const DbusValue *dbus_arg(const DbusValue *args, isize i) { return args && i < args->count ? &args->items[i] : NULL; }
// Variants unwrapped; "" / 0 when absent or of another type.
StringView dbus_value_string(const DbusValue *v);
i64 dbus_value_int(const DbusValue *v);

// ---- Messages ----------------------------------------------------------------------------------

typedef struct DbusMsg DbusMsg;

DbusMsg *dbus_msg_method(StringView destination, StringView path, StringView interface, StringView method);
DbusMsg *dbus_msg_signal(StringView path, StringView interface, StringView member, StringView destination);
void dbus_msg_free(DbusMsg *m);
void dbus_msg_string(DbusMsg *m, StringView s);
void dbus_msg_path(DbusMsg *m, StringView path);
void dbus_msg_int32(DbusMsg *m, i32 v);
void dbus_msg_uint32(DbusMsg *m, u32 v);
void dbus_msg_bool(DbusMsg *m, bool v);
void dbus_msg_fd(DbusMsg *m, i32 fd);   // the message gets its own copy of the descriptor
// a{sv}: begin, entries, end. Nested containers are not needed by the callers.
void dbus_msg_dict_begin(DbusMsg *m);
void dbus_msg_dict_string(DbusMsg *m, StringView key, StringView value);
void dbus_msg_dict_uint32(DbusMsg *m, StringView key, u32 value);
void dbus_msg_dict_bool(DbusMsg *m, StringView key, bool value);
void dbus_msg_dict_end(DbusMsg *m);
// a(sa{sv}) / other shapes for test services: one struct of (u, a{sv}) per stream.
void dbus_msg_array_begin(DbusMsg *m, const char *element_signature);
void dbus_msg_struct_begin(DbusMsg *m);
void dbus_msg_container_end(DbusMsg *m);

// Sends a method call and waits for the reply. On success `reply` is an 'r' value
// holding the reply's arguments. Frees `m`.
bool dbus_link_call(DbusLink *link, DbusMsg *m, i32 timeout_ms, DbusValue *reply, String *error);
bool dbus_link_send(DbusLink *link, DbusMsg *m);   // no reply expected; frees `m`

// Waits for signal `member` of `interface` on `path` (matched first with
// dbus_link_add_match). `args` is an 'r' value. False on timeout, when
// *cancel turns true, or when the connection is lost.
bool dbus_link_wait_signal(DbusLink *link, StringView path, StringView interface, StringView member, i32 timeout_ms,
                           const volatile bool *cancel, DbusValue *args);

// ---- Being a service (tests) ---------------------------------------------------------------------

typedef struct DbusCall {
    void *message;     // the incoming call (owned)
    String path, interface, member, sender;
    DbusValue args;    // 'r'
} DbusCall;

// The next method call addressed to this link, within `timeout_ms`.
bool dbus_link_next_call(DbusLink *link, i32 timeout_ms, DbusCall *out);
DbusMsg *dbus_msg_reply(const DbusCall *call);
DbusMsg *dbus_msg_error(const DbusCall *call, StringView name, StringView text);
void dbus_call_free(DbusCall *call);

// Object path a portal request gets for `token` on this link (Request docs:
// /org/freedesktop/portal/desktop/request/SENDER/TOKEN, SENDER = the unique name
// without ':' and with '.' -> '_').
String dbus_portal_request_path(const DbusLink *link, StringView token);
