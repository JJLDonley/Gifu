// libdbus-1 through dlopen. The few public types are declared here exactly as in
// libdbus' own headers (dbus-message.h, dbus-errors.h, dbus-shared.h of D-Bus 1.14),
// which are part of its stable ABI.
#include "platform/dbus_link.h"

#include "base/log.h"
#include "base/vec.h"
#include "platform/os.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef u32 dbus_bool_t;
typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct DBusMessageIter {   // dbus-message.h
    void *dummy1, *dummy2;
    u32 dummy3;
    int dummy4, dummy5, dummy6, dummy7, dummy8, dummy9, dummy10, dummy11;
    int pad1;
    void *pad2, *pad3;
} DBusMessageIter;
typedef struct DBusError {   // dbus-errors.h
    const char *name;
    const char *message;
    unsigned int dummy1 : 1, dummy2 : 1, dummy3 : 1, dummy4 : 1, dummy5 : 1;
    void *padding1;
} DBusError;
enum { DBUS_BUS_SESSION = 0 };                                        // dbus-shared.h
enum { MSG_METHOD_CALL = 1, MSG_METHOD_RETURN = 2, MSG_ERROR = 3, MSG_SIGNAL = 4 };   // dbus-protocol.h
enum { NAME_FLAG_DO_NOT_QUEUE = 4 };

// ---- The library ------------------------------------------------------------------------------

static struct {
    bool tried, ok;
    dbus_bool_t (*threads_init_default)(void);
    void (*error_init)(DBusError *);
    void (*error_free)(DBusError *);
    dbus_bool_t (*error_is_set)(const DBusError *);
    DBusConnection *(*bus_get_private)(int, DBusError *);
    const char *(*bus_get_unique_name)(DBusConnection *);
    void (*bus_add_match)(DBusConnection *, const char *, DBusError *);
    int (*bus_request_name)(DBusConnection *, const char *, unsigned int, DBusError *);
    void (*connection_close)(DBusConnection *);
    void (*connection_unref)(DBusConnection *);
    void (*connection_set_exit_on_disconnect)(DBusConnection *, dbus_bool_t);
    dbus_bool_t (*connection_send)(DBusConnection *, DBusMessage *, u32 *);
    DBusMessage *(*connection_send_with_reply_and_block)(DBusConnection *, DBusMessage *, int, DBusError *);
    dbus_bool_t (*connection_read_write)(DBusConnection *, int);
    DBusMessage *(*connection_pop_message)(DBusConnection *);
    void (*connection_flush)(DBusConnection *);
    DBusMessage *(*message_new_method_call)(const char *, const char *, const char *, const char *);
    DBusMessage *(*message_new_method_return)(DBusMessage *);
    DBusMessage *(*message_new_signal)(const char *, const char *, const char *);
    DBusMessage *(*message_new_error)(DBusMessage *, const char *, const char *);
    void (*message_unref)(DBusMessage *);
    dbus_bool_t (*message_set_destination)(DBusMessage *, const char *);
    int (*message_get_type)(DBusMessage *);
    const char *(*message_get_path)(DBusMessage *);
    const char *(*message_get_interface)(DBusMessage *);
    const char *(*message_get_member)(DBusMessage *);
    const char *(*message_get_sender)(DBusMessage *);
    dbus_bool_t (*message_iter_init)(DBusMessage *, DBusMessageIter *);
    void (*message_iter_init_append)(DBusMessage *, DBusMessageIter *);
    dbus_bool_t (*message_iter_append_basic)(DBusMessageIter *, int, const void *);
    dbus_bool_t (*message_iter_open_container)(DBusMessageIter *, int, const char *, DBusMessageIter *);
    dbus_bool_t (*message_iter_close_container)(DBusMessageIter *, DBusMessageIter *);
    int (*message_iter_get_arg_type)(DBusMessageIter *);
    void (*message_iter_get_basic)(DBusMessageIter *, void *);
    dbus_bool_t (*message_iter_next)(DBusMessageIter *);
    void (*message_iter_recurse)(DBusMessageIter *, DBusMessageIter *);
} D;

bool dbus_link_available(void) {
    if (D.tried) return D.ok;
    D.tried = true;
    void *lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        log_info("dbus: libdbus-1.so.3 not found (%s)", dlerror());
        return false;
    }
    bool ok = true;
#define SYM(field, name) ok &= ((*(void **)&D.field = dlsym(lib, name)) != NULL)
    SYM(threads_init_default, "dbus_threads_init_default");
    SYM(error_init, "dbus_error_init");
    SYM(error_free, "dbus_error_free");
    SYM(error_is_set, "dbus_error_is_set");
    SYM(bus_get_private, "dbus_bus_get_private");
    SYM(bus_get_unique_name, "dbus_bus_get_unique_name");
    SYM(bus_add_match, "dbus_bus_add_match");
    SYM(bus_request_name, "dbus_bus_request_name");
    SYM(connection_close, "dbus_connection_close");
    SYM(connection_unref, "dbus_connection_unref");
    SYM(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    SYM(connection_send, "dbus_connection_send");
    SYM(connection_send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
    SYM(connection_read_write, "dbus_connection_read_write");
    SYM(connection_pop_message, "dbus_connection_pop_message");
    SYM(connection_flush, "dbus_connection_flush");
    SYM(message_new_method_call, "dbus_message_new_method_call");
    SYM(message_new_method_return, "dbus_message_new_method_return");
    SYM(message_new_signal, "dbus_message_new_signal");
    SYM(message_new_error, "dbus_message_new_error");
    SYM(message_unref, "dbus_message_unref");
    SYM(message_set_destination, "dbus_message_set_destination");
    SYM(message_get_type, "dbus_message_get_type");
    SYM(message_get_path, "dbus_message_get_path");
    SYM(message_get_interface, "dbus_message_get_interface");
    SYM(message_get_member, "dbus_message_get_member");
    SYM(message_get_sender, "dbus_message_get_sender");
    SYM(message_iter_init, "dbus_message_iter_init");
    SYM(message_iter_init_append, "dbus_message_iter_init_append");
    SYM(message_iter_append_basic, "dbus_message_iter_append_basic");
    SYM(message_iter_open_container, "dbus_message_iter_open_container");
    SYM(message_iter_close_container, "dbus_message_iter_close_container");
    SYM(message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
    SYM(message_iter_get_basic, "dbus_message_iter_get_basic");
    SYM(message_iter_next, "dbus_message_iter_next");
    SYM(message_iter_recurse, "dbus_message_iter_recurse");
#undef SYM
    if (!ok) {
        log_warn("dbus: libdbus-1 lacks expected functions");
        return false;
    }
    D.threads_init_default();   // links are used from capture threads
    D.ok = true;
    return true;
}

// ---- Links ------------------------------------------------------------------------------------

struct DbusLink {
    DBusConnection *conn;
    String unique;
};

// "what: message (org.example.Error.Name)": callers match on the error name.
static String take_error(DBusError *e, const char *what) {
    String s = string_format("%s: %s (%s)", what, e->message ? e->message : "unknown error", e->name ? e->name : "?");
    D.error_free(e);
    return s;
}

static void set_error(String *error, String message) {
    if (error) {
        string_free(error);
        *error = message;
    } else {
        string_free(&message);
    }
}

DbusLink *dbus_link_open_session(String *error) {
    if (!dbus_link_available()) {
        set_error(error, string_from(SV("libdbus-1 is not installed")));
        return NULL;
    }
    DBusError e;
    D.error_init(&e);
    DBusConnection *conn = D.bus_get_private(DBUS_BUS_SESSION, &e);
    if (!conn) {
        set_error(error, take_error(&e, "cannot connect to the session bus"));
        return NULL;
    }
    D.connection_set_exit_on_disconnect(conn, 0);   // never take the program down
    DbusLink *link = calloc(1, sizeof(DbusLink));
    if (!link) abort();
    link->conn = conn;
    const char *name = D.bus_get_unique_name(conn);
    link->unique = string_from(sv(name ? name : ""));
    return link;
}

void dbus_link_close(DbusLink *link) {
    if (!link) return;
    D.connection_close(link->conn);
    D.connection_unref(link->conn);
    string_free(&link->unique);
    free(link);
}

StringView dbus_link_unique_name(const DbusLink *link) { return string_sv(&link->unique); }

bool dbus_link_add_match(DbusLink *link, StringView rule, String *error) {
    String r = string_from(rule);
    DBusError e;
    D.error_init(&e);
    D.bus_add_match(link->conn, string_cstr(&r), &e);
    string_free(&r);
    if (D.error_is_set(&e)) {
        set_error(error, take_error(&e, "AddMatch"));
        return false;
    }
    return true;
}

bool dbus_link_request_name(DbusLink *link, StringView name, String *error) {
    String n = string_from(name);
    DBusError e;
    D.error_init(&e);
    int rc = D.bus_request_name(link->conn, string_cstr(&n), NAME_FLAG_DO_NOT_QUEUE, &e);
    string_free(&n);
    if (D.error_is_set(&e)) {
        set_error(error, take_error(&e, "RequestName"));
        return false;
    }
    return rc == 1;   // DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
}

String dbus_portal_request_path(const DbusLink *link, StringView token) {
    String path = string_from(SV("/org/freedesktop/portal/desktop/request/"));
    StringView u = string_sv(&link->unique);
    if (u.len && u.data[0] == ':') u = sv_drop(u, 1);
    for (isize i = 0; i < u.len; ++i) string_push_char(&path, u.data[i] == '.' ? '_' : u.data[i]);
    string_push_char(&path, '/');
    string_push(&path, token);
    return path;
}

// ---- Values -----------------------------------------------------------------------------------

void dbus_value_free(DbusValue *v) {
    if (!v) return;
    for (isize i = 0; i < v->count; ++i) dbus_value_free(&v->items[i]);
    free(v->items);
    string_free(&v->s);
    if (v->type == 'h' && v->fd >= 0) close(v->fd);
    *v = (DbusValue){ 0 };
}

static const DbusValue *unwrap(const DbusValue *v) {
    while (v && v->type == 'v' && v->count == 1) v = &v->items[0];
    return v;
}

const DbusValue *dbus_dict_get(const DbusValue *dict, StringView key) {
    dict = unwrap(dict);
    if (!dict || dict->type != 'a') return NULL;
    for (isize i = 0; i < dict->count; ++i) {
        const DbusValue *e = &dict->items[i];
        if (e->type == 'e' && e->count == 2 && sv_eq(string_sv(&e->items[0].s), key)) return unwrap(&e->items[1]);
    }
    return NULL;
}

StringView dbus_value_string(const DbusValue *v) {
    v = unwrap(v);
    return v && (v->type == 's' || v->type == 'o' || v->type == 'g') ? string_sv(&v->s) : SV("");
}

i64 dbus_value_int(const DbusValue *v) {
    v = unwrap(v);
    return v ? v->i : 0;
}

// Reads the element at `it` (and, for containers, everything inside it).
static void read_value(DBusMessageIter *it, DbusValue *out) {
    *out = (DbusValue){ .type = (char)D.message_iter_get_arg_type(it), .fd = -1 };
    switch (out->type) {
        case 's': case 'o': case 'g': {
            const char *s = NULL;
            D.message_iter_get_basic(it, &s);
            out->s = string_from(sv(s ? s : ""));
        } break;
        case 'b': { u32 b = 0; D.message_iter_get_basic(it, &b); out->i = b != 0; } break;
        case 'y': { u8 b = 0; D.message_iter_get_basic(it, &b); out->i = b; } break;
        case 'n': { i16 b = 0; D.message_iter_get_basic(it, &b); out->i = b; } break;
        case 'q': { u16 b = 0; D.message_iter_get_basic(it, &b); out->i = b; } break;
        case 'i': { i32 b = 0; D.message_iter_get_basic(it, &b); out->i = b; } break;
        case 'u': { u32 b = 0; D.message_iter_get_basic(it, &b); out->i = b; } break;
        case 'x': { i64 b = 0; D.message_iter_get_basic(it, &b); out->i = b; } break;
        case 't': { u64 b = 0; D.message_iter_get_basic(it, &b); out->i = (i64)b; } break;
        case 'd': { f64 b = 0; D.message_iter_get_basic(it, &b); out->d = b; } break;
        case 'h': { int fd = -1; D.message_iter_get_basic(it, &fd); out->fd = fd; } break;   // a duplicate we own
        case 'a': case 'r': case 'e': case 'v': {
            DBusMessageIter sub;
            D.message_iter_recurse(it, &sub);
            Vec(DbusValue) items = { 0 };
            while (D.message_iter_get_arg_type(&sub) != 0) {
                DbusValue v;
                read_value(&sub, &v);
                vec_push(&items, v);
                if (!D.message_iter_next(&sub)) break;
            }
            out->items = items.items;
            out->count = items.len;
        } break;
        default: break;
    }
}

static void read_args(DBusMessage *msg, DbusValue *out) {
    *out = (DbusValue){ .type = 'r', .fd = -1 };
    DBusMessageIter it;
    if (!D.message_iter_init(msg, &it)) return;
    Vec(DbusValue) items = { 0 };
    do {
        if (D.message_iter_get_arg_type(&it) == 0) break;
        DbusValue v;
        read_value(&it, &v);
        vec_push(&items, v);
    } while (D.message_iter_next(&it));
    out->items = items.items;
    out->count = items.len;
}

// ---- Messages ---------------------------------------------------------------------------------

enum { MAX_DEPTH = 8 };

struct DbusMsg {
    DBusMessage *msg;
    DBusMessageIter iters[MAX_DEPTH];
    i32 depth;   // iters[depth] is where the next argument goes
};

static DbusMsg *wrap(DBusMessage *msg) {
    if (!msg) return NULL;
    DbusMsg *m = calloc(1, sizeof(DbusMsg));
    if (!m) abort();
    m->msg = msg;
    D.message_iter_init_append(msg, &m->iters[0]);
    return m;
}

DbusMsg *dbus_msg_method(StringView dest, StringView path, StringView iface, StringView method) {
    if (!dbus_link_available()) return NULL;
    String a = string_from(dest), b = string_from(path), c = string_from(iface), d = string_from(method);
    DbusMsg *m = wrap(D.message_new_method_call(string_cstr(&a), string_cstr(&b), string_cstr(&c), string_cstr(&d)));
    string_free(&a), string_free(&b), string_free(&c), string_free(&d);
    return m;
}

DbusMsg *dbus_msg_signal(StringView path, StringView iface, StringView member, StringView dest) {
    if (!dbus_link_available()) return NULL;
    String a = string_from(path), b = string_from(iface), c = string_from(member), d = string_from(dest);
    DbusMsg *m = wrap(D.message_new_signal(string_cstr(&a), string_cstr(&b), string_cstr(&c)));
    if (m && dest.len) D.message_set_destination(m->msg, string_cstr(&d));
    string_free(&a), string_free(&b), string_free(&c), string_free(&d);
    return m;
}

void dbus_msg_free(DbusMsg *m) {
    if (!m) return;
    D.message_unref(m->msg);
    free(m);
}

static void append(DbusMsg *m, int type, const void *value) {
    if (m) D.message_iter_append_basic(&m->iters[m->depth], type, value);
}

static void append_cstr(DbusMsg *m, int type, StringView s) {
    String c = string_from(s);
    const char *p = string_cstr(&c);
    append(m, type, &p);
    string_free(&c);
}

void dbus_msg_string(DbusMsg *m, StringView s) { append_cstr(m, 's', s); }
void dbus_msg_path(DbusMsg *m, StringView path) { append_cstr(m, 'o', path); }
void dbus_msg_int32(DbusMsg *m, i32 v) { append(m, 'i', &v); }
void dbus_msg_uint32(DbusMsg *m, u32 v) { append(m, 'u', &v); }
void dbus_msg_bool(DbusMsg *m, bool v) {
    u32 b = v;
    append(m, 'b', &b);
}
void dbus_msg_fd(DbusMsg *m, i32 fd) {
    int f = fd;
    append(m, 'h', &f);
}

static void open_container(DbusMsg *m, int type, const char *sig) {
    if (!m || m->depth + 1 >= MAX_DEPTH) return;
    D.message_iter_open_container(&m->iters[m->depth], type, sig, &m->iters[m->depth + 1]);
    m->depth++;
}

void dbus_msg_container_end(DbusMsg *m) {
    if (!m || m->depth == 0) return;
    D.message_iter_close_container(&m->iters[m->depth - 1], &m->iters[m->depth]);
    m->depth--;
}

void dbus_msg_array_begin(DbusMsg *m, const char *element_signature) { open_container(m, 'a', element_signature); }
void dbus_msg_struct_begin(DbusMsg *m) { open_container(m, 'r', NULL); }
void dbus_msg_dict_begin(DbusMsg *m) { open_container(m, 'a', "{sv}"); }
void dbus_msg_dict_end(DbusMsg *m) { dbus_msg_container_end(m); }

static void dict_entry(DbusMsg *m, StringView key, const char *sig, int type, const void *value, StringView str) {
    open_container(m, 'e', NULL);
    dbus_msg_string(m, key);
    open_container(m, 'v', sig);
    if (type == 's') append_cstr(m, 's', str);
    else append(m, type, value);
    dbus_msg_container_end(m);
    dbus_msg_container_end(m);
}

void dbus_msg_dict_string(DbusMsg *m, StringView key, StringView value) { dict_entry(m, key, "s", 's', NULL, value); }
void dbus_msg_dict_uint32(DbusMsg *m, StringView key, u32 value) { dict_entry(m, key, "u", 'u', &value, SV("")); }
void dbus_msg_dict_bool(DbusMsg *m, StringView key, bool value) {
    u32 b = value;
    dict_entry(m, key, "b", 'b', &b, SV(""));
}

bool dbus_link_call(DbusLink *link, DbusMsg *m, i32 timeout_ms, DbusValue *reply, String *error) {
    *reply = (DbusValue){ .type = 'r', .fd = -1 };
    if (!m) {
        set_error(error, string_from(SV("D-Bus message could not be made")));
        return false;
    }
    DBusError e;
    D.error_init(&e);
    DBusMessage *r = D.connection_send_with_reply_and_block(link->conn, m->msg, timeout_ms, &e);
    dbus_msg_free(m);
    if (!r) {
        set_error(error, take_error(&e, "D-Bus call failed"));
        return false;
    }
    read_args(r, reply);
    D.message_unref(r);
    return true;
}

bool dbus_link_send(DbusLink *link, DbusMsg *m) {
    if (!m) return false;
    bool ok = D.connection_send(link->conn, m->msg, NULL) != 0;
    D.connection_flush(link->conn);
    dbus_msg_free(m);
    return ok;
}

static bool cstr_eq(const char *a, StringView b) { return a && sv_eq(sv(a), b); }

bool dbus_link_wait_signal(DbusLink *link, StringView path, StringView iface, StringView member, i32 timeout_ms,
                           const volatile bool *cancel, DbusValue *args) {
    *args = (DbusValue){ .type = 'r', .fd = -1 };
    f64 until = os_time_seconds() + timeout_ms / 1000.0;
    for (;;) {
        DBusMessage *msg;
        while ((msg = D.connection_pop_message(link->conn)) != NULL) {
            bool match = D.message_get_type(msg) == MSG_SIGNAL && cstr_eq(D.message_get_path(msg), path) &&
                         cstr_eq(D.message_get_interface(msg), iface) && cstr_eq(D.message_get_member(msg), member);
            if (match) read_args(msg, args);
            D.message_unref(msg);
            if (match) return true;
        }
        if ((cancel && *cancel) || os_time_seconds() >= until) return false;
        if (!D.connection_read_write(link->conn, 50)) return false;   // disconnected
    }
}

// ---- Service side (tests) ---------------------------------------------------------------------

bool dbus_link_next_call(DbusLink *link, i32 timeout_ms, DbusCall *out) {
    *out = (DbusCall){ 0 };
    f64 until = os_time_seconds() + timeout_ms / 1000.0;
    for (;;) {
        DBusMessage *msg;
        while ((msg = D.connection_pop_message(link->conn)) != NULL) {
            if (D.message_get_type(msg) != MSG_METHOD_CALL) {
                D.message_unref(msg);
                continue;
            }
            const char *p = D.message_get_path(msg), *i = D.message_get_interface(msg), *mm = D.message_get_member(msg),
                       *s = D.message_get_sender(msg);
            out->message = msg;
            out->path = string_from(sv(p ? p : ""));
            out->interface = string_from(sv(i ? i : ""));
            out->member = string_from(sv(mm ? mm : ""));
            out->sender = string_from(sv(s ? s : ""));
            read_args(msg, &out->args);
            return true;
        }
        if (os_time_seconds() >= until) return false;
        if (!D.connection_read_write(link->conn, 50)) return false;
    }
}

DbusMsg *dbus_msg_reply(const DbusCall *call) { return wrap(D.message_new_method_return(call->message)); }

DbusMsg *dbus_msg_error(const DbusCall *call, StringView name, StringView text) {
    String n = string_from(name), t = string_from(text);
    DbusMsg *m = wrap(D.message_new_error(call->message, string_cstr(&n), string_cstr(&t)));
    string_free(&n), string_free(&t);
    return m;
}

void dbus_call_free(DbusCall *call) {
    if (call->message) D.message_unref(call->message);
    string_free(&call->path), string_free(&call->interface), string_free(&call->member), string_free(&call->sender);
    dbus_value_free(&call->args);
    *call = (DbusCall){ 0 };
}
