/**
 * @brief Provides C++ interface to DBus.
 * 
 * This uses the low level library libdbus.
 * 
 * The following features are supported:
 *  - Remote objects:
 *      - Discovery by specifying a list of interfaces
 *      - Method calls
 *      - Subscribing to signals
 *  - Local objects:
 *      - Exposing with a list of interfaces
 *      - Local Object Manager
 *      - Method calls
 *      - Emitting local signals
 * 
 * The following features are not supported:
 *  - Remote Properties
 *  - Local Properties
 *  - Introspection interface on Remote Objects
 *  - Introspection interface on Local Objects
 *  - Returning an error on async method calls
 */

#ifndef __FIBRE_DBUS_HPP
#define __FIBRE_DBUS_HPP

// helpful reference: http://www.matthew.ath.cx/misc/dbus

#include <dbus/dbus.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <limits.h>
#include <string.h>
#include <fibre/platform_support/linux_worker.hpp>
#include <fibre/platform_support/linux_timer.hpp>
#include <fibre/cpp_utils.hpp>
#include <fibre/print_utils.hpp>
#include <fibre/logging.hpp>
#include <unistd.h>

DEFINE_LOG_TOPIC(DBUS);
#define current_log_topic LOG_TOPIC_DBUS

namespace std {
template<typename ... Ts>
static std::ostream& operator<<(std::ostream& stream, DBusMessage& msg) {
    const char* interface_name = dbus_message_get_interface(&msg);
    const char* method_name = dbus_message_get_member(&msg);
    const char* object_path = dbus_message_get_path(&msg);
    return stream << "DBusMessage (" << dbus_message_get_type(&msg) << "): "
            << " intf: " << (interface_name ? interface_name : "(null)")
            << ", member: " << (method_name ? method_name : "(null)")
            << ", object: " << (object_path ? object_path : "(null)");
}
}

namespace fibre {

/* Forward declarations ------------------------------------------------------*/

class DBusConnectionWrapper;

/**
 * @brief Implements message push/pop functions for each supported type.
 * 
 * This template is specialized for every type that this DBus wrapper supports.
 * 
 * Each specialization shall have the following static members:
 * - `type_id` shall be an integer_constant<int> holding a DBus type id
 * - `signature` shall be a sstring holding the full DBus signature of the type as string
 * - int push(DBusMessageIter* iter, T val) shall append the given value to a message
 * - int pop(DBusMessageIter* iter, T& val) shall dequeue a value of type T from the
 *   message and assign it to val.
 */
template<typename T, typename=std::true_type>
struct DBusTypeTraits;

template<size_t I, size_t N, typename T>
struct variant_helper;

struct DBusObjectPath : std::string {
    DBusObjectPath() = default;
    DBusObjectPath(const DBusObjectPath &) = default;
    DBusObjectPath(std::string str) : std::string(str) {}
    using std::string::string;
};

// @brief A std::variant supporting the types most commonly used in DBus variants
struct dbus_variant;

using dbus_variant_base = std::variant<
    std::string, bool, DBusObjectPath,
    /* int8_t (char on most platforms) is not supported by DBus */
    short, int, long, long long,
    unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long,
    std::vector<std::string>
>;

struct dbus_variant : dbus_variant_base {
    using dbus_variant_base::dbus_variant_base;
};

using dbus_type_id_t = std::string; // Used as a key in internal data structures

using FunctionImplTable = std::unordered_map<fibre::dbus_type_id_t, int(*)(void*, DBusMessage*, DBusMessage*)>;
using ExportTableBase = std::unordered_map<std::string, FunctionImplTable>;


// TODO: this is quite general and could reside outside of DBus
template<typename ... TArgs>
class DBusSignal {
public:
    DBusSignal() {}

    DBusSignal& operator+=(Callback<TArgs...>* callback) {
        callbacks_.push_back(callback);
        return *this;
    }

    DBusSignal& operator-=(Callback<TArgs...>* callback) {
        auto it = std::find(callbacks_.begin(), callbacks_.end(), callback);
        if (it != callbacks_.end()) {
            callbacks_.erase(it);
        } else {
            FIBRE_LOG(E) << "attempt to deregister a callback more than once";
        }
        return *this;
    }

    void trigger(TArgs... args) const {
        for (auto it: callbacks_) {
            if (it) {
                (*it)(args...);
            }
        }
    }

    size_t size() const { return callbacks_.size(); }

private:
    std::vector<Callback<TArgs...>*> callbacks_;
};


/* Function definitions ------------------------------------------------------*/

template<typename T>
dbus_type_id_t get_type_id() { return std::string(typeid(T).name()); }

/**
 * @brief Appends the given arguments to the message iterator.
 */
template<typename ... Ts>
static int pack_message(DBusMessageIter* iter, Ts ... args);

template<>
inline int pack_message(DBusMessageIter* iter) {
    return 0;
}

template<typename T, typename ... Ts>
inline int pack_message(DBusMessageIter* iter, T arg, Ts... args) {
    if (DBusTypeTraits<T>::push(iter, arg) != 0) {
        FIBRE_LOG(E) << "Failed to pack message\n";
        return -1;
    }
    int result = pack_message<Ts...>(iter, args...);
    return result;
}


template<typename ... Ts, size_t ... Is>
inline int pack_message(DBusMessage* msg, std::tuple<Ts...> args, std::index_sequence<Is...>) {
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    return pack_message(&iter, std::get<Is>(args)...);
}


/**
 * @brief Unpacks the message into the given references.
 */
template<typename ... Ts>
static int unpack_message(DBusMessageIter* iter, Ts& ... args);

template<>
inline int unpack_message(DBusMessageIter* iter) {
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_INVALID) {
        FIBRE_LOG(E) << "Too many arguments";
        return -1;
    }
    return 0;
}

template<typename T, typename ... Ts>
inline int unpack_message(DBusMessageIter* iter, T& arg, Ts&... args) {
    if (dbus_message_iter_get_arg_type(iter) != DBusTypeTraits<T>::type_id::value) {
        FIBRE_LOG(E) << "Argument type mismatch. Expected " << DBusTypeTraits<T>::type_id::value << ", got " << dbus_message_iter_get_arg_type(iter);
        return -1;
    }
    if (DBusTypeTraits<T>::pop(iter, arg) != 0) {
        FIBRE_LOG(E) << "Failed to unpack message";
        return -1;
    }
    dbus_message_iter_next(iter);
    return unpack_message<Ts...>(iter, args...);
}

template<typename ... Ts, std::size_t... I>
int unpack_message_to_tuple_impl(DBusMessageIter* iter, std::tuple<Ts...>& tuple, std::index_sequence<I...>) {
    return unpack_message(iter, std::get<I>(tuple)...);
}

template<typename ... Ts>
int unpack_message_to_tuple(DBusMessageIter* iter, std::tuple<Ts...>& tuple) {
    return unpack_message_to_tuple_impl(iter, tuple, std::make_index_sequence<sizeof...(Ts)>());
}

template<typename ... Ts>
int unpack_message(DBusMessage* message, std::tuple<Ts...>& tuple, int expected_type = DBUS_MESSAGE_TYPE_INVALID) {
    DBusMessageIter args;
    dbus_message_iter_init(message, &args);

    if (expected_type != DBUS_MESSAGE_TYPE_INVALID) {
        int type = dbus_message_get_type(message);
        if (type == DBUS_MESSAGE_TYPE_ERROR) {
            std::string error_msg;
            if (unpack_message(&args, error_msg) != 0) {
                FIBRE_LOG(E) << "DBus error received but failted to unpack error message.";
            } else {
                FIBRE_LOG(E) << "DBus error received: " << error_msg;
            }
            return -1;
        }
        if (type != expected_type) {
            FIBRE_LOG(E) << "unexpected message with type " << type;
            return -1;
        }
    }

    if (unpack_message_to_tuple(&args, tuple) != 0) {
        FIBRE_LOG(E) << "Failed to unpack message content.";
        return -1;
    }

    FIBRE_LOG(D) << "message unpacking complete";
    return 0;
}

template<typename TInterface, typename ... TOutputs>
void handle_reply_message(DBusMessage* msg, TInterface* obj, Callback<TInterface*, TOutputs...>* callback) {
    std::tuple<TOutputs...> values;

    if (unpack_message(msg, values, DBUS_MESSAGE_TYPE_METHOD_RETURN) != 0) {
        FIBRE_LOG(E) << "Failed to unpack reply. Will not invoke callback.";
        // TODO: invoke error callback
        return;
    }

    if (callback) {
        apply(*callback, std::tuple_cat(std::make_tuple(obj), values));
    }
}

template<typename TInterface, typename ... TArgs, size_t ... I>
void handle_signal_message(DBusMessage* msg, TInterface* obj, const DBusSignal<TInterface*, TArgs...>& signal, std::index_sequence<I...>) {
    std::tuple<TArgs...> values;
    if (unpack_message(msg, values, DBUS_MESSAGE_TYPE_SIGNAL) != 0) {
        FIBRE_LOG(E) << "Failed to unpack signal. Will not invoke callback.";
        return;
    }

    signal.trigger(obj, std::get<I>(values)...);
}


/* Class definitions ---------------------------------------------------------*/

class DBusConnectionWrapper {
public:
    struct obj_table_entry_t {
        dbus_type_id_t type_id;
        void* ptr;
        size_t intf_count; // number of interfaces associated with the object
    };
    int init(LinuxWorker* worker, bool system_bus = false);
    int deinit();

    DBusConnection* get_libdbus_ptr() { return conn_; }
    std::string get_name() { return dbus_bus_get_unique_name(get_libdbus_ptr()); }

    /**
     * @brief Registers an object such that incoming DBus method calls can be
     * routed to the corresponding implementation.
     * 
     * A object is not automatically discoverable when it is registered. See
     * DBusLocalObjectManager to publish objects in a discoverable way.
     * 
     * An interface list must be specified to indicate which interfaces the
     * object implements. There will be ugly compiler errors if the object does
     * not implement the specified interfaces. [TODO: make them less ugly]
     * 
     * The same object instance can be registered multiple times with different
     * interfaces and different paths, however the same path cannot be used for
     * two different objects.
     *
     * @param obj: A reference to the object instance. Must remain valid until
     *        all interfaces were deregistered using deregister_interfaces().
     * @param path: The DBus object path under which the object should be
     *        exposed. The path must start with a slash ("/").
     */
    template<typename ... TInterfaces, typename TImpl>
    int register_interfaces(TImpl& obj, DBusObjectPath path) {
        if (path.compare(0, 1, "/") != 0) {
            FIBRE_LOG(E) << "path must start with a slash";
            return -1;
        }

        auto it = object_table.insert({path, {get_type_id<TImpl>(), &obj, 0}});
        if (it.second) {
            if (it.first->second.type_id != get_type_id<TImpl>() || it.first->second.ptr != &obj) {
                FIBRE_LOG(E) << "attempt to register new object under existing path";
                return -1;
            }
        }

        int dummy[] = { (register_interface<TInterfaces>(path, obj), 0)... };
        it.first->second.intf_count += sizeof...(TInterfaces);
        return 0;
    }

    /**
     * @brief Same as the other register_interfaces overload, except that a unique object path is autogenerated
     * The object path will have the form "/__obj_[N]__" where N is an integer.
     * 
     * @param path: Will be set to the autogenerated path. Can be null.
     */
    template<typename ... TInterfaces, typename TImpl>
    int register_interfaces(TImpl& obj, DBusObjectPath* path = nullptr) {
        DBusObjectPath temp;
        if (!path)
            path = &temp;
        *path = "/__obj_" + std::to_string(object_table.size()) + "__";
        return register_interfaces<TInterfaces...>(obj, *path);
    }

    template<typename ... TInterfaces>
    int deregister_interfaces(DBusObjectPath path) {
        auto it = object_table.find(path);
        if (it == object_table.end()) {
            FIBRE_LOG(E) << "object " << path << " was not registered";
            return -1;
        }

        // Deregister interface implementions for this type.
        // If multiple objects with the same type and interface were registered,
        // this will just reduce a ref count.
        dbus_type_id_t type_id = it->second.type_id;
        int results[] = { deregister_interface<TInterfaces>(path, it->second.ptr, it->second.type_id)... };

        size_t success = 0;
        for (auto it2 : results)
            if (it2 == 0)
                success++;

        // Remove object from object table if all it's interfaces were deregistered
        if (success > it->second.intf_count) {
            FIBRE_LOG(E) << "deregistered more interfaces than registered";
        }
        it->second.intf_count -= std::max(success, it->second.intf_count);
        if (it->second.intf_count == 0) {
            object_table.erase(it);
        }
        return (success == sizeof...(TInterfaces)) ? 0 : -1;
    }

    template<typename ... TOutputs, typename ... TInputs>
    static int handle_method_call_typed(DBusMessage* rx_msg, DBusMessage* tx_msg, const Callable<std::tuple<TOutputs...>, TInputs...>& method) {
        std::tuple<TInputs...> inputs;
        if (unpack_message(rx_msg, inputs) != 0) {
            FIBRE_LOG(E) << "Failed to unpack method call. Will not invoke handler.";
            return -1;
        }

        std::tuple<TOutputs...> outputs = apply(method, inputs);

        if (pack_message(tx_msg, outputs, std::make_index_sequence<sizeof...(TOutputs)>()) != 0) {
            FIBRE_LOG(E) << "failed to pack args";
            return -1;
        }

        return 0;
    }

    /**
     * @brief Notifies remote DBus applications that the specified signal has
     * triggered.
     * 
     * The signal may not be emitted immediately, to do so you must call
     * dbus_connection_flush().
     * 
     * TODO: ordering guarantees? DBus orders method calls and method replies
     * but what about signals?
     * see https://www.freedesktop.org/wiki/IntroductionToDBus/#messageordering
     */
    template<typename TInterface, typename ... TArgs>
    void emit_signal(std::string signal_name, DBusObjectPath path, TArgs... args) {
        DBusMessage* tx_msg = dbus_message_new_signal(path.c_str(), TInterface::get_interface_name(), signal_name.c_str());
        if (!tx_msg) {
            FIBRE_LOG(E) << "message is NULL";
            return;
        }

        if (pack_message(tx_msg, std::forward_as_tuple(args...), std::make_index_sequence<sizeof...(TArgs)>()) != 0) {
            FIBRE_LOG(E) << "failed to pack args";
            return;
        }

        if (!dbus_connection_send(get_libdbus_ptr(), tx_msg, nullptr)) {
            FIBRE_LOG(E) << "failed to send signal";
            dbus_message_unref(tx_msg);
            return;
        }

        dbus_message_unref(tx_msg);
        return;
    }

private:

    int handle_add_watch(DBusWatch *watch);
    void handle_remove_watch(DBusWatch *watch);
    int handle_toggle_watch(DBusWatch *watch, bool enable);
    void handle_watch(DBusWatch *watch, uint32_t events);

    int handle_add_timeout(DBusTimeout* timeout);
    void handle_remove_timeout(DBusTimeout* timeout);
    int handle_toggle_timeout(DBusTimeout *timeout, bool enable);
    void handle_timeout(DBusTimeout *timeout);

    void handle_dispatch();

    DBusHandlerResult handle_method_call(DBusMessage* rx_msg);

    static DBusHandlerResult handle_method_call_stub(DBusConnection *connection, DBusMessage *message, void *user_data) {
        return ((DBusConnectionWrapper*)user_data)->handle_method_call(message);
    }

    template<typename T>
    ExportTableBase* construct_export_table() {
        static T singleton{};
        return &singleton;
    }

    template<typename TInterface, typename TImpl>
    void register_interface(std::string path, TImpl& obj) {
        // Create and register exactly one instance of the export table of the
        // interface and then register this implementation type with the
        // export table.
        using exporter_t = typename TInterface::ExportTable;
        auto insert_result = interface_table.insert({TInterface::get_interface_name(), construct_export_table<exporter_t>()});
        exporter_t* intf = reinterpret_cast<exporter_t*>(insert_result.first->second);
        intf->register_implementation(*this, path, obj);
    }

    template<typename TInterface>
    int deregister_interface(std::string path, void* obj, dbus_type_id_t type_id) {
        using exporter_t = typename TInterface::ExportTable;
        auto it = interface_table.find(TInterface::get_interface_name());
        if (it == interface_table.end()) {
            FIBRE_LOG(E) << "attempt to deregister an interface too many times";
            return -1;
        }
        exporter_t* intf = reinterpret_cast<exporter_t*>(it->second);
        if (intf->deregister_implementation(*this, path, obj, type_id) != 0) {
            FIBRE_LOG(E) << "attempt to deregister implementation too many times";
            return -1;
        }
        if (intf->ref_count.size() == 0) {
            interface_table.erase(it);
        }
        return 0;
    }

    using watch_ctx_t = bind_result_t<member_closure_t<decltype(&DBusConnectionWrapper::handle_watch)>, DBusWatch*>;

    struct timeout_ctx_t {
        LinuxTimer timer;
        bind_result_t<member_closure_t<decltype(&DBusConnectionWrapper::handle_timeout)>, DBusTimeout*> callback;
    };

    DBusError err_;
    DBusConnection* conn_;
    LinuxWorker* worker_;

    LinuxAutoResetEvent dispatch_signal_ = LinuxAutoResetEvent("dbus dispatch");
    member_closure_t<decltype(&DBusConnectionWrapper::handle_dispatch)> handle_dispatch_obj_{&DBusConnectionWrapper::handle_dispatch, this};

    // Lookup tables to route incoming method calls to the correct receiver
    std::unordered_map<std::string, obj_table_entry_t> object_table{};
    std::unordered_map<std::string, ExportTableBase*> interface_table{};
};


class DBusRemoteObjectBase {
public:
    DBusRemoteObjectBase()
        : conn_(nullptr), service_name_(""), object_name_("") {}

    DBusRemoteObjectBase(DBusConnectionWrapper* conn, std::string service_name, std::string object_name) 
        : conn_(conn), service_name_(service_name), object_name_(object_name) {}

    inline bool operator==(const DBusRemoteObjectBase & other) const {
        return (conn_ == other.conn_) && (service_name_ == other.service_name_)
            && (object_name_ == other.object_name_);
    }

    inline bool operator!=(const DBusRemoteObjectBase & other) const {
        return (conn_ != other.conn_) || (service_name_ != other.service_name_)
            || (object_name_ != other.object_name_);
    }

    template<typename TInterface, typename ... TInputs, typename ... TOutputs>
    int method_call_async(TInterface* obj, const char* method_name, Callback<TInterface*, TOutputs...>* callback, TInputs ... inputs) {
        DBusMessage* msg;
        DBusMessageIter args;
        DBusPendingCall* pending;
        dbus_bool_t status;

        using pending_call_ctx_t = struct { TInterface* obj; Callback<TInterface*, TOutputs...>* callback; };
        auto pending_call_handler = [](DBusPendingCall* pending, void* ctx_unsafe){
            pending_call_ctx_t* ctx = reinterpret_cast<pending_call_ctx_t*>(ctx_unsafe);
            DBusMessage* msg = dbus_pending_call_steal_reply(pending);
            handle_reply_message(msg, ctx->obj, ctx->callback);
            dbus_pending_call_unref(pending); // this will deallocate the memory being pointed to by ctx
            dbus_message_unref(msg);
        };

        // TODO: we get a segfault if we try to use a service name which does
        // not include a dot. Find out if this is libdbus' or our fault and
        // possibly file a bug report.

        msg = dbus_message_new_method_call(service_name_.c_str(), // target for the method call
                object_name_.c_str(), // object to call on
                TInterface::get_interface_name(), // interface to call on
                method_name); // method name
        if (!msg) {
            FIBRE_LOG(E) << "Message Null";
            goto fail1;
        }

        dbus_message_iter_init_append(msg, &args);
        if (pack_message(&args, inputs...) != 0) {
            FIBRE_LOG(E) << "failed to pack args";
            goto fail2;
        }

        // send message and get a handle for a reply
        if (!dbus_connection_send_with_reply(conn_->get_libdbus_ptr(), msg, &pending, -1)) { // -1 is default timeout
            FIBRE_LOG(E) << "Out Of Memory!";
            goto fail2;
        }
        if (!pending) { 
            FIBRE_LOG(E) << "Pending Call Null";
            goto fail2; 
        }
        dbus_connection_flush(conn_->get_libdbus_ptr()); // TODO: not sure if we should flush here

        // free output message
        dbus_message_unref(msg);
        FIBRE_LOG(D) << "dispatched method call message";

        status = dbus_pending_call_set_notify(pending, pending_call_handler,
                new pending_call_ctx_t{obj, callback},
                [](void* ctx){ delete (pending_call_ctx_t*)ctx; });
        if (!status) {
            FIBRE_LOG(E) << "failed to set pending call callback";
            goto fail1;
        }

        // Handle reply now if it already arrived before we set the notify callback.
        msg = dbus_pending_call_steal_reply(pending);
        if (msg) {
            dbus_pending_call_unref(pending);
            handle_reply_message(msg, obj, callback);
            dbus_message_unref(msg);
        }

        return 0;

fail2:
        dbus_message_unref(msg);
fail1:
        return -1;
    }

    DBusConnectionWrapper* conn_;
    std::string service_name_;
    std::string object_name_;
};

template<typename TInterface, typename ... TArgs>
class DBusRemoteSignal {
public:
    DBusRemoteSignal(TInterface* parent, const char* name)
        : parent_(parent), name_(name)
    { }

    ~DBusRemoteSignal() {
        if (signal_.size() > 0) {
            FIBRE_LOG(W) << "not all clients have unsubscribed from this event";
            deactivate_filter();
        }
    }

    DBusRemoteSignal& operator+=(Callback<TInterface*, TArgs...>* callback) {
        signal_ += callback;
        if (signal_.size() > 0 && !is_active) {
            if (activate_filter() != 0) {
                FIBRE_LOG(E) << "failed to activate remote signal subscription";
            } else {
                is_active = true;
            }
        }
        return *this;
    }

    DBusRemoteSignal& operator-=(Callback<TInterface*, TArgs...>* callback) {
        signal_ -= callback;
        if (signal_.size() == 0 && is_active) {
            if (deactivate_filter() != 0) {
                FIBRE_LOG(E) << "failed to deactivate remote signal subscription";
            } else {
                is_active = false;
            }
        }
        return *this;
    }

private:
    static DBusHandlerResult filter_callback(DBusConnection *connection, DBusMessage *message, void *user_data) {
        if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL) {
            DBusRemoteSignal* self = (DBusRemoteSignal*)user_data;

            // TODO: compare sender
            // The sender may be reported as ":1.10" even if the match was registered as "sender='org.bluez'"
            bool matches = (strcmp(dbus_message_get_interface(message), TInterface::get_interface_name()) == 0)
                    && (strcmp(dbus_message_get_member(message), self->name_) == 0)
                    && (strcmp(dbus_message_get_path(message), self->parent_->base_->object_name_.c_str()) == 0);

            if (matches) {
                FIBRE_LOG(D) << "received signal " << *message;
                handle_signal_message(message, self->parent_, self->signal_, std::make_index_sequence<sizeof...(TArgs)>()); // TODO: error handling
                return DBUS_HANDLER_RESULT_HANDLED;
            } else {
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    };

    int activate_filter() {
        if (!parent_ || !parent_->base_ || !parent_->base_->conn_) {
            FIBRE_LOG(E) << "object not initialized properly";
            return -1;
        }

        if (!dbus_connection_add_filter(parent_->base_->conn_->get_libdbus_ptr(), filter_callback, this, nullptr)) {
            FIBRE_LOG(E) << "failed to add filter";
            return -1;
        }

        std::string rule = "type='signal',"
            "sender='" + parent_->base_->service_name_ + "',"
            "interface='" + TInterface::get_interface_name() + "',"
            "member='" + std::string(name_) + "',"
            "path='" + parent_->base_->object_name_ + "'";
        
        FIBRE_LOG(D) << "adding rule " << rule << " to connection";
        dbus_bus_add_match(parent_->base_->conn_->get_libdbus_ptr(), rule.c_str(), nullptr);
        dbus_connection_flush(parent_->base_->conn_->get_libdbus_ptr());
        return 0;
    }

    int deactivate_filter() {
        dbus_connection_remove_filter(parent_->base_->conn_->get_libdbus_ptr(), filter_callback, this);
        return 0;
    }

    TInterface* parent_;
    const char* name_;
    DBusSignal<TInterface*, TArgs...> signal_;
    bool is_active = false;
};


template<typename ... TInterfaces>
class DBusRemoteObject : public TInterfaces... {
public:
    DBusRemoteObject(DBusRemoteObjectBase base)
        : base_(base), TInterfaces(&base_)... {}

    DBusRemoteObjectBase base_;
};


/* DBus Type Traits ----------------------------------------------------------*/

template<size_t BITS, bool SIGNED> constexpr int get_int_type_id();
template<> constexpr int get_int_type_id<16, true>() { return DBUS_TYPE_INT16; }
template<> constexpr int get_int_type_id<32, true>() { return DBUS_TYPE_INT32; }
template<> constexpr int get_int_type_id<64, true>() { return DBUS_TYPE_INT64; }
template<> constexpr int get_int_type_id<8, false>() { return DBUS_TYPE_BYTE; }
template<> constexpr int get_int_type_id<16, false>() { return DBUS_TYPE_UINT16; }
template<> constexpr int get_int_type_id<32, false>() { return DBUS_TYPE_UINT32; }
template<> constexpr int get_int_type_id<64, false>() { return DBUS_TYPE_UINT64; }

template<size_t BITS, bool SIGNED> struct get_int_signature;
template<> struct get_int_signature<16, true> { using type = MAKE_SSTRING(DBUS_TYPE_INT16_AS_STRING); };
template<> struct get_int_signature<32, true> { using type = MAKE_SSTRING(DBUS_TYPE_INT32_AS_STRING); };
template<> struct get_int_signature<64, true> { using type = MAKE_SSTRING(DBUS_TYPE_INT64_AS_STRING); };
template<> struct get_int_signature<8, false> { using type = MAKE_SSTRING(DBUS_TYPE_BYTE_AS_STRING); };
template<> struct get_int_signature<16, false> { using type = MAKE_SSTRING(DBUS_TYPE_UINT16_AS_STRING); };
template<> struct get_int_signature<32, false> { using type = MAKE_SSTRING(DBUS_TYPE_UINT32_AS_STRING); };
template<> struct get_int_signature<64, false> { using type = MAKE_SSTRING(DBUS_TYPE_UINT64_AS_STRING); };


template<typename T>
struct DBusTypeTraits<T, typename std::is_integral<T>::type> {
    static constexpr size_t BITS = sizeof(T) * CHAR_BIT;
    using type_id = std::integral_constant<int, get_int_type_id<BITS, std::is_signed<T>::value>()>;
    static constexpr auto signature = typename get_int_signature<BITS, std::is_signed<T>::value>::type{};

    static int push(DBusMessageIter* iter, T val) {
        return dbus_message_iter_append_basic(iter, type_id::value, &val) ? 0 : -1;
    }

    static int pop(DBusMessageIter* iter, T& val) {
        dbus_message_iter_get_basic(iter, &val);
        return 0;
    }
};

template<>
struct DBusTypeTraits<bool> {
    using type_id = std::integral_constant<int, DBUS_TYPE_BOOLEAN>;
    static constexpr auto signature = MAKE_SSTRING(DBUS_TYPE_BOOLEAN_AS_STRING){};

    // BOOLEAN values are marshalled as 32-bit integers. Only 0 and 1 are valid.
    // Source: https://dbus.freedesktop.org/doc/dbus-specification.html#idm646

    static int push(DBusMessageIter* iter, bool val) {
        uint32_t val_as_int = val ? 1 : 0;
        return dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &val_as_int) ? 0 : -1;
    }

    static int pop(DBusMessageIter* iter, bool& val) {
        uint32_t val_as_int = 0;
        dbus_message_iter_get_basic(iter, &val_as_int);
        if (val_as_int == 0) {
            val = false;
        } else if (val_as_int == 1) {
            val = true;
        } else {
            FIBRE_LOG(E) << "Invalid boolean value " << val_as_int;
            return -1;
        }
        return 0;
    }
};

template<>
struct DBusTypeTraits<std::string> {
    using type_id = std::integral_constant<int, DBUS_TYPE_STRING>;
    static constexpr auto signature = MAKE_SSTRING(DBUS_TYPE_STRING_AS_STRING){};

    static int push(DBusMessageIter* iter, std::string val) {
        const char* str = val.c_str();
        int result = dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &str) ? 0 : -1;
        return result;
    }

    static int pop(DBusMessageIter* iter, std::string& val) {
        char* c_str = nullptr;
        dbus_message_iter_get_basic(iter, &c_str);
        if (c_str == nullptr) {
            FIBRE_LOG(E) << "Popped invalid string";
            return -1;
        }
        val = std::string(c_str);
        return 0;
    }
};

/* TODO: a tuple should probably correspond to a DBus struct
template<typename ... TElement>
struct DBusTypeTraits<std::tuple<TElement...>> {
    using type_id = std::integral_constant<int, DBUS_TYPE_STRUCT>;

    static int pop(DBusMessageIter* iter, std::tuple<TElement...>& val) {
        DBusMessageIter subiter;
        dbus_message_iter_recurse(iter, &subiter);
        unpack_message_to_tuple(&subiter, val);
        return 0;
    }
};
*/

template<typename TElement>
struct DBusTypeTraits<std::vector<TElement>> {
    using type_id = std::integral_constant<int, DBUS_TYPE_ARRAY>;
    static constexpr auto signature = MAKE_SSTRING(DBUS_TYPE_ARRAY_AS_STRING){} +
                                      DBusTypeTraits<TElement>::signature;

    static int push(DBusMessageIter* iter, std::vector<TElement> val) {
        DBusMessageIter subiter;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, DBusTypeTraits<TElement>::signature.c_str(), &subiter)) {
            FIBRE_LOG(E) << "failed to open container";
            return -1;
        }
        for (TElement& el: val) {
            if (DBusTypeTraits<TElement>::push(&subiter, el) != 0) {
                FIBRE_LOG(E) << "failed to append array element";
                return -1;
            }
        }
        if (!dbus_message_iter_close_container(iter, &subiter)) {
            FIBRE_LOG(E) << "failed to close container";
            return -1;
        }
        return 0;
    }

    static int pop(DBusMessageIter* iter, std::vector<TElement>& val) {
        DBusMessageIter subiter;
        dbus_message_iter_recurse(iter, &subiter);
        while (dbus_message_iter_get_arg_type(&subiter) != DBUS_TYPE_INVALID) {
            TElement element;
            if (DBusTypeTraits<TElement>::pop(&subiter, element) != 0)
                return -1;
            val.push_back(element);
            dbus_message_iter_next(&subiter);
        }
        return 0;
    }
};


template<typename TKey, typename TVal>
struct DBusTypeTraits<std::unordered_map<TKey, TVal>> {
    using type_id = std::integral_constant<int, DBUS_TYPE_ARRAY>;
    static constexpr auto element_signature = 
            MAKE_SSTRING(DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING){} +
            DBusTypeTraits<TKey>::signature + DBusTypeTraits<TVal>::signature +
            MAKE_SSTRING(DBUS_DICT_ENTRY_END_CHAR_AS_STRING){};
    static constexpr auto signature = MAKE_SSTRING(DBUS_TYPE_ARRAY_AS_STRING){} +
                                      element_signature;

    static int push(DBusMessageIter* iter, std::unordered_map<TKey, TVal> val) {
        static auto elem_sig = element_signature;
        DBusMessageIter dict;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, elem_sig.c_str(), &dict)) {
            FIBRE_LOG(E) << "failed to open dict container";
            return -1;
        }

        for (auto& it: val) {
            DBusMessageIter entry;
            if (!dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry)) {
                FIBRE_LOG(E) << "failed to open dict entry container";
                return -1;
            }
            DBusTypeTraits<TKey>::push(&entry, it.first);
            DBusTypeTraits<TVal>::push(&entry, it.second);
            if (!dbus_message_iter_close_container(&dict, &entry)) {
                FIBRE_LOG(E) << "failed to close container";
                return -1;
            }
        }

        if (!dbus_message_iter_close_container(iter, &dict)) {
            FIBRE_LOG(E) << "failed to close container";
            return -1;
        }
        return 0;
    }

    static int pop(DBusMessageIter* iter, std::unordered_map<TKey, TVal>& val) {
        //val 
        DBusMessageIter dict;
        dbus_message_iter_recurse(iter, &dict);
        while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            TKey dict_entry_key;
            TVal dict_entry_val;
            DBusMessageIter dict_entry;
            dbus_message_iter_recurse(&dict, &dict_entry);
            if (unpack_message(&dict_entry, dict_entry_key, dict_entry_val) != 0) {
                FIBRE_LOG(E) << "failed to unpack dict entry";
                return -1;
            }
            val[dict_entry_key] = dict_entry_val;
            dbus_message_iter_next(&dict);
        }
        if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
            FIBRE_LOG(E) << "dict contains something else than dict entry";
            return -1;
        }
        return 0;
    }
};


template<>
struct DBusTypeTraits<DBusObjectPath> {
    using type_id = std::integral_constant<int, DBUS_TYPE_OBJECT_PATH>;
    static constexpr auto signature = MAKE_SSTRING(DBUS_TYPE_OBJECT_PATH_AS_STRING){};

    static int push(DBusMessageIter* iter, DBusObjectPath val) {
        const char * object_path = val.c_str();
        return dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &object_path) ? 0 : -1;
    }

    static int pop(DBusMessageIter* iter, DBusObjectPath& val) {
        return DBusTypeTraits<std::string>::pop(iter, val);
    }
};

/** @brief Helper class for pushing and popping variants */
template<size_t I, size_t N, typename ... Ts>
struct variant_helper<I, N, std::variant<Ts...>> {
    using subtype = std::tuple_element_t<I, std::tuple<Ts...>>;
    using next_helper = variant_helper<(I+1), N, std::variant<Ts...>>;

    static const char* get_element_signature(std::variant<Ts...>& val) {
        if (val.index() == I) {
            return DBusTypeTraits<subtype>::signature.c_str();
        }
        return next_helper::get_element_signature(val);
    }

    static int push(DBusMessageIter* iter, std::variant<Ts...>& val) {
        if (val.index() == I) {
            return DBusTypeTraits<subtype>::push(iter, std::get<I>(val));
        }
        return next_helper::push(iter, val);
    }

    static int pop(DBusMessageIter* iter, std::variant<Ts...>& val, const char* signature) {
        if (strcmp(DBusTypeTraits<subtype>::signature.c_str(), signature) == 0) {
            subtype inner_val{};
            int result = DBusTypeTraits<subtype>::pop(iter, inner_val);
            val = std::variant<Ts...>(inner_val);
            return result;
        }
        return next_helper::pop(iter, val, signature);
    }

};

/** @brief Helper class for pushing and popping variants */
template<size_t I, typename ... Ts>
struct variant_helper<I, I, std::variant<Ts...>> {
    static const char* get_element_signature(std::variant<Ts...>& val) {
        FIBRE_LOG(E) << "variant implementation broken";
        return nullptr;
    }

    static int push(DBusMessageIter* iter, std::variant<Ts...>& val) {
        FIBRE_LOG(E) << "variant implementation broken";
        return -1;
    }

    static int pop(DBusMessageIter* iter, std::variant<Ts...>& val, const char* signature) {
        FIBRE_LOG(E) << "signature " << signature << " not supported by this variant implementation";
        return -1;
    }
};

template<typename ... Ts>
struct DBusTypeTraits<std::variant<Ts...>> {
    using type_id = std::integral_constant<int, DBUS_TYPE_VARIANT>;
    static constexpr auto signature = MAKE_SSTRING(DBUS_TYPE_VARIANT_AS_STRING){};

    using helper = variant_helper<0, sizeof...(Ts), std::variant<Ts...>>;

    static int push(DBusMessageIter* iter, std::variant<Ts...>& val) {
        DBusMessageIter subiter;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, helper::get_element_signature(val), &subiter)) {
            FIBRE_LOG(E) << "failed to open container";
            return -1;
        }
        int result = helper::push(&subiter, val);
        if (!dbus_message_iter_close_container(iter, &subiter)) {
            FIBRE_LOG(E) << "failed to close container";
            result = -1;
        }
        return result;
    }

    static int pop(DBusMessageIter* iter, std::variant<Ts...>& val) {
        DBusMessageIter subiter;
        dbus_message_iter_recurse(iter, &subiter);
        char* signature = dbus_message_iter_get_signature(&subiter);
        int result = helper::pop(&subiter, val, signature);
        dbus_free(signature);
        return result;
    }
};

template<>
struct DBusTypeTraits<dbus_variant> : DBusTypeTraits<dbus_variant_base> {};

}


/* Extensions for std namespace ----------------------------------------------*/

namespace std {

static std::ostream& operator<<(std::ostream& stream, const fibre::DBusRemoteObjectBase& val) {
    return stream << val.object_name_ << " @ " << val.service_name_ << "";
}

template<typename ... TInterfaces>
static std::ostream& operator<<(std::ostream& stream, const fibre::DBusRemoteObject<TInterfaces...>& val) {
    return stream << val->base_ << "";
}

template<typename ... TInterfaces>
struct hash<fibre::DBusRemoteObject<TInterfaces...>> {
    size_t operator()(const fibre::DBusRemoteObject<TInterfaces...>& obj) const {
        size_t hashes[] = { std::hash<TInterfaces>(obj)... };
        size_t result = 0;
        for (size_t i = 0; i < sizeof...(TInterfaces); ++i) {
            result += hashes[i];
        }
        return result;
    }
};

template<>
struct hash<fibre::DBusObjectPath> {
    size_t operator()(const fibre::DBusObjectPath& obj) const {
        return hash<string>()(obj);
    }
};

}


// TODO: this file is getting complex. Maybe we should move this to another file?
namespace fibre {

//class org_freedesktop_DBus_ObjectManager; // defined in autogenerated header file
#include "../../../platform_support/dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "../../../platform_support/dbus_interfaces/org.freedesktop.DBus.Properties.hpp"


template<typename ... TInterfaces>
class DBusDiscoverer {
public:
    using proxy_t = DBusRemoteObject<TInterfaces...>;

    int start(org_freedesktop_DBus_ObjectManager* obj_manager, Callback<proxy_t*>* on_object_found, Callback<proxy_t*>* on_object_lost) {
        obj_manager_ = obj_manager; // TODO: check if already started
        on_object_found_ = on_object_found;
        on_object_lost_ = on_object_lost;
        
        scan_completed_ = false;
        obj_manager_->InterfacesAdded += &handle_interfaces_added_obj;
        obj_manager_->InterfacesRemoved += &handle_interfaces_removed_obj;
        obj_manager_->GetManagedObjects_async(&handle_scan_complete_obj);
        return 0;
    }

    int stop() {
        // TODO: check if already stopped
        obj_manager_->InterfacesAdded -= &handle_interfaces_added_obj; // TODO: error handling
        obj_manager_->InterfacesRemoved -= &handle_interfaces_removed_obj;
        // TODO: cancel potentially ongoing GetManagedObjects call
        // TODO: there should be two levels of stop:
        //  - "Stop notifying me about new objects because I have what I was
        //    looking for. Don't tear down the objects that you found, but let
        //    me know when they disappear."
        //  - "Stop notifying me about new objects and tear down all objects
        //    that have been discovered so far."

        obj_manager_ = nullptr;
        return 0;
    }

private:
    struct impl_table_t {
        std::array<bool, sizeof...(TInterfaces)> is_implemented{};
        proxy_t* instance = nullptr;
    };

    using interface_map = std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>;
    
    void handle_interfaces_added(org_freedesktop_DBus_ObjectManager* obj_mgr, DBusObjectPath obj, interface_map new_interfaces) {
        if (!scan_completed_)
            return;

        auto obj_iterator = implementation_matrix_.end();

        // Register in implementation map which interfaces were added to this object
        for (size_t i = 0; i < sizeof...(TInterfaces); ++i) { // TODO: swap this for better loop unrolling
            if (new_interfaces.find(interface_names_[i]) != new_interfaces.end()) {
                if (obj_iterator == implementation_matrix_.end())
                    obj_iterator = implementation_matrix_.insert({obj, {}}).first;
                obj_iterator->second.is_implemented[i] = true;
            }
        }

        // If all interfaces have been added, notify the client
        if (obj_iterator != implementation_matrix_.end()) {
            bool is_full = true;
            for (size_t i = 0; i < sizeof...(TInterfaces); ++i) {
                is_full &= obj_iterator->second.is_implemented[i];
            }
            if (is_full) {
                FIBRE_LOG(D) << "discovered all interfaces of object " << obj;
                if (obj_iterator->second.instance) {
                    FIBRE_LOG(E) << "object already exists";
                } else {
                    obj_iterator->second.instance = new proxy_t(DBusRemoteObjectBase(obj_mgr->base_->conn_, obj_mgr->base_->service_name_, obj));
                    if (on_object_found_) {
                        (*on_object_found_)(obj_iterator->second.instance);
                    }
                }
            }
        }
    }

    void handle_interfaces_removed(org_freedesktop_DBus_ObjectManager*, DBusObjectPath obj, std::vector<std::string> old_interfaces) {
        if (!scan_completed_)
            return;

        auto obj_iterator = implementation_matrix_.end();

        // Register in implementation map which interfaces were removed from this object
        for (size_t i = 0; i < sizeof...(TInterfaces); ++i) { // TODO: swap this for better loop unrolling
            for (size_t j = 0; j < old_interfaces.size(); ++j) {
                if (interface_names_[i] == old_interfaces[j]) {
                    if (obj_iterator == implementation_matrix_.end())
                        obj_iterator = implementation_matrix_.find(obj);
                    if (obj_iterator == implementation_matrix_.end()) {
                        // this should never happen
                        FIBRE_LOG(E) << "tried to remove interface before it was added";
                    } else {
                        obj_iterator->second.is_implemented[i] = false;
                    }
                }
            }
        }

        // If all interfaces have been removed, remove the object itself
        if (obj_iterator != implementation_matrix_.end()) {
            FIBRE_LOG(D) << "discovered all interfaces of object " << obj;
            if (!obj_iterator->second.instance) {
                FIBRE_LOG(E) << "object does not exist";
            } else {
                if (on_object_lost_) {
                    (*on_object_lost_)(obj_iterator->second.instance);
                }
                delete obj_iterator->second.instance;
                obj_iterator->second.instance = nullptr;
            }

            bool is_empty = true;
            for (size_t i = 0; i < sizeof...(TInterfaces); ++i) {
                is_empty &= !obj_iterator->second.is_implemented[i];
            }
            if (is_empty) {
                implementation_matrix_.erase(obj_iterator);
                FIBRE_LOG(D) << "lost all interfaces of object " << obj;
                // TODO: notify client
            }
        }
    }

    void handle_scan_complete(org_freedesktop_DBus_ObjectManager* obj_mgr, std::unordered_map<DBusObjectPath, interface_map> objects) {
        scan_completed_ = true;
        FIBRE_LOG(D) << "found " << objects.size() << " objects";
        for (auto& it : objects) {
            handle_interfaces_added(obj_mgr, it.first, it.second);
        }
    }

    const char * interface_names_[sizeof...(TInterfaces)] = { TInterfaces::get_interface_name()... };
    org_freedesktop_DBus_ObjectManager* obj_manager_ = nullptr;
    Callback<proxy_t*>* on_object_found_ = nullptr;
    Callback<proxy_t*>* on_object_lost_ = nullptr;
    std::unordered_map<DBusObjectPath, impl_table_t> implementation_matrix_{};
    bool scan_completed_ = false;

    member_closure_t<decltype(&DBusDiscoverer::handle_interfaces_added)> handle_interfaces_added_obj{&DBusDiscoverer::handle_interfaces_added, this};
    member_closure_t<decltype(&DBusDiscoverer::handle_interfaces_removed)> handle_interfaces_removed_obj{&DBusDiscoverer::handle_interfaces_removed, this};
    member_closure_t<decltype(&DBusDiscoverer::handle_scan_complete)> handle_scan_complete_obj{&DBusDiscoverer::handle_scan_complete, this};
};

class DBusLocalObjectManager {
public:
    int init(DBusConnectionWrapper* conn, std::string path) {
        if (conn_) {
            FIBRE_LOG(E) << "already initialized";
            return -1;
        }
        conn_ = conn;
        name_ = path;

        // TODO: should the object manager publish itself?
        if (conn_->register_interfaces<org_freedesktop_DBus_ObjectManager>(*this, name_) != 0) {
            FIBRE_LOG(E) << "failed to expose object";
            goto fail1;
        }

        return 0;

fail1:
        conn_ = nullptr;
        name_ = "";
        return -1;
    }

    int deinit() {
        if (!conn_) {
            FIBRE_LOG(E) << "not initialized";
            return -1;
        }

        if (conn_->deregister_interfaces<org_freedesktop_DBus_ObjectManager>(name_) != 0) {
            FIBRE_LOG(E) << "failed to deregister object";
            return -1;
        }
        if (obj_table.size() != 0) {
            FIBRE_LOG(E) << "attempt to deinit non-empty object manager";
            return -1;
        }
        
        conn_ = nullptr;
        name_ = "";
        return 0;
    }

    using managed_object_dict_t = std::unordered_map<fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>;
    managed_object_dict_t GetManagedObjects() {
        FIBRE_LOG(D) << "GetManagedObjects() got called on " << name_;

        managed_object_dict_t result{};
        for (auto obj_it : obj_table) {
            result[obj_it.first] = get_interface_dict(obj_it.second, obj_it.second.interfaces);
        }
        return result;
    }
    
    template<typename ... TInterfaces, typename TImpl>
    int add_interfaces(TImpl& obj, std::string name) {
        if (name.compare(0, 1, "/") == 0) {
            FIBRE_LOG(E) << "path should not start with a slash";
            return -1;
        }

        DBusObjectPath obj_path = name_ + "/" + name;
        if (conn_->register_interfaces<TInterfaces...>(obj, obj_path) != 0) {
            FIBRE_LOG(E) << "failed to expose object";
            return -1;
        }

        std::vector<std::string> intf_names{TInterfaces::get_interface_name()...};
        auto& obj_entry = obj_table.insert({obj_path, obj_table_entry_t{}}).first->second;
        for (auto intf_it : intf_names) {
            obj_entry.interfaces.push_back(intf_it);
        }

        int dummy[] = {(add_prop_fn(obj, obj_entry, typename TInterfaces::tag{}), 0)...};
        InterfacesAdded.trigger(obj_path, get_interface_dict(obj_entry, intf_names));
        return 0;
    }

    template<typename ... TInterfaces>
    int remove_interfaces(std::string name) {
        DBusObjectPath obj_path = name_ + "/" + name;

        auto it = obj_table.find(obj_path);
        if (it == obj_table.end()) {
            FIBRE_LOG(E) << "not published";
            return -1;
        }
        obj_table_entry_t& obj_entry = it->second;

        const char * interface_names[] = {TInterfaces::get_interface_name()...};
        for (size_t i = 0; i < sizeof...(TInterfaces); ++i) {
            auto rm = std::find(obj_entry.interfaces.begin(), obj_entry.interfaces.end(), interface_names[i]);
            if (rm == obj_entry.interfaces.end()) {
                FIBRE_LOG(E) << "not all of these interfaces were published";
                // TODO: return error code
            } else {
                obj_entry.interfaces.erase(rm);
            }
        }

        int dummy[] = {(rm_prop_fn(obj_entry, typename TInterfaces::tag{}), 0)...};

        if (obj_entry.interfaces.size() == 0) {
            obj_table.erase(it);
        }
        if (conn_->deregister_interfaces<TInterfaces...>(obj_path) != 0) {
            FIBRE_LOG(E) << "failed to deregister object";
            return -1;
        }

        // TODO: If the client calls GetManagedObjects after the object has been
        // removed from the internal data structures but before InterfacesRemoved
        // was triggered, the client may get a signal for an unknown object.
        std::vector<std::string> intf_names{TInterfaces::get_interface_name()...};
        InterfacesRemoved.trigger(obj_path, intf_names);
        return 0;
    }

    /**
     * @brief Returns the DBus object path under which this object manager is
     * registered.
     * 
     * This is at the same time the root of the object hierarchy that is managed
     * by this object manager.
     */
    std::string get_path() { return name_; }

    DBusSignal<DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>> InterfacesAdded;
    DBusSignal<DBusObjectPath, std::vector<std::string>> InterfacesRemoved;

private:
    using prop_list_t = std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>;

    struct obj_table_entry_t {
        std::vector<std::string> interfaces;
        Callable<std::unordered_map<std::string, fibre::dbus_variant>, std::string>* get_props = nullptr;
    };

    template<typename TImpl>
    static void add_prop_fn(TImpl& obj, obj_table_entry_t& entry, typename org_freedesktop_DBus_Properties::tag) {
        auto get_all_closure = make_closure(&TImpl::GetAll, &obj);
        entry.get_props = new decltype(get_all_closure)(get_all_closure);
    }

    template<typename TImpl, typename TTag>
    static void add_prop_fn(TImpl& obj, obj_table_entry_t& entry, TTag) { }

    static void rm_prop_fn(obj_table_entry_t& entry, typename org_freedesktop_DBus_Properties::tag) {
        if (entry.get_props) {
            delete entry.get_props;
            entry.get_props = nullptr;
        }
    }

    template<typename TTag>
    static void rm_prop_fn(obj_table_entry_t& entry, TTag) { }

    /**
     * @brief Returns a dictionary that contains the given interface names as
     * keys, along with a snapshot of all properties (and values) of each
     * interface.
     * 
     * @param interfaces_to_add: THe interfaces to be added to the result dict.
     * @param all_interfaces: All interfaces for this object. Used for fetching the properties.
     */
    prop_list_t get_interface_dict(obj_table_entry_t& obj_entry, std::vector<std::string> interfaces_to_add) {
        prop_list_t result{};

        for (auto intf_it : interfaces_to_add) {
            if (obj_entry.get_props) {
                result[intf_it] = (*obj_entry.get_props)(intf_it);
            } else {
                result[intf_it] = {};
            }
        }

        FIBRE_LOG(D) << "properties snapshot: " << result;
        return result;
    }

    DBusConnectionWrapper* conn_ = nullptr;
    std::string name_ = "";
    std::unordered_map<std::string, obj_table_entry_t> obj_table{};
};

}

#undef current_log_topic

#endif // __FIBRE_DBUS_HPP