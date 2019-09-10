#ifndef __FIBRE_DBUS_HPP
#define __FIBRE_DBUS_HPP

// helpful reference: http://www.matthew.ath.cx/misc/dbus

#include <dbus/dbus.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <limits.h>
#include <string.h>
#include <fibre/worker.hpp>
#include <fibre/cpp_utils.hpp>
#include <fibre/print_utils.hpp>
#include <fibre/logging.hpp>

DEFINE_LOG_TOPIC(DBUS);
#define current_log_topic LOG_TOPIC_DBUS

namespace fibre {

/* Forward declarations ------------------------------------------------------*/

class DBusConnectionWrapper;
class DBusObject;

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

// @brief A std::variant supporting the types most commonly used in DBus variants
using dbus_variant = std::variant<
    std::string, bool, DBusObject,
    /* int8_t (char on most platforms) is not supported by DBus */
    short, int, long, long long,
    unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long,
    std::vector<std::string>
>;

using dbus_type_id_t = std::string; // Used as a key in internal data structures

using FunctionImplTable = std::unordered_map<fibre::dbus_type_id_t, int(*)(void*, DBusMessage*, DBusMessage*)>;
using ExportTableBase = std::unordered_map<std::string, FunctionImplTable>;


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

template<typename ... TOutputs>
void handle_reply_message(DBusMessage* msg, Callback<TOutputs...>* callback) {
    std::tuple<TOutputs...> values;

    if (unpack_message(msg, values, DBUS_MESSAGE_TYPE_METHOD_RETURN) != 0) {
        FIBRE_LOG(E) << "Failed to unpack reply. Will not invoke callback.";
        // TODO: invoke error callback
        return;
    }

    if (callback) {
        apply(*callback, values);
    }
}

template<typename ... TArgs>
void handle_signal_message(DBusMessage* msg, const std::vector<Callback<TArgs...>*>& callbacks) {
    std::tuple<TArgs...> values;
    if (unpack_message(msg, values, DBUS_MESSAGE_TYPE_SIGNAL) != 0) {
        FIBRE_LOG(E) << "Failed to unpack signal. Will not invoke callback.";
        return;
    }

    for (auto& callback : callbacks) {
        if (callback) {
            apply(*callback, values);
        }
    }
}


/* Class definitions ---------------------------------------------------------*/

class DBusConnectionWrapper {
public:
    int init(Worker* worker, bool system_bus = false);
    int deinit();

    DBusConnection* get_libdbus_ptr() { return conn_; }

    template<typename ... TInterfaces, typename TImpl>
    int publish(std::string name, TImpl& obj) {
        // TODO: Generate object path transparently from object instance.
        object_table[name] = std::make_tuple(get_type_id<TImpl>(), &obj);
        int dummy[] = { publish_with_interface<TInterfaces>(obj)... };
        for (int& it : dummy)
            if (it)
                return -1;
        return 0;
    }

    // TODO: implement unpublish

    template<typename ... TOutputs, typename ... TInputs>
    static int handle_method_call_typed(DBusMessage* rx_msg, DBusMessage* tx_msg, const GenericFunction<std::tuple<TOutputs...>, TInputs...>& method) {
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
    int publish_with_interface(TImpl& obj) {
        // Create and register exactly one instance of the export table of the
        // interface and then register this implementation type with the
        // export table.
        using exporter_t = typename TInterface::ExportTable;
        auto insert_result = interface_table.insert({TInterface::get_interface_name(), construct_export_table<exporter_t>()});
        exporter_t* intf = reinterpret_cast<exporter_t*>(insert_result.first->second);
        return intf->register_implementation(obj);
    }

    DBusError err_;
    DBusConnection* conn_;
    Worker* worker_;

    Signal dispatch_signal_ = Signal("dbus dispatch");
    Closure<DBusConnectionWrapper, std::tuple<DBusConnectionWrapper*>, std::tuple<>, void> dispatch_callback_obj_{&DBusConnectionWrapper::handle_dispatch, this};

    // Lookup tables to route incoming method calls to the correct receiver
    std::unordered_map<std::string, std::tuple<dbus_type_id_t, void*>> object_table{};
    std::unordered_map<std::string, ExportTableBase*> interface_table{};
};


class DBusObject {
public:
    DBusObject()
        : conn_(nullptr), service_name_(""), object_name_("") {}

    DBusObject(DBusConnectionWrapper* conn, std::string service_name, std::string object_name) 
        : conn_(conn), service_name_(service_name), object_name_(object_name) {}

    inline bool operator==(const DBusObject & other) const {
        return (conn_ == other.conn_) && (service_name_ == other.service_name_)
            && (object_name_ == other.object_name_);
    }

    inline bool operator!=(const DBusObject & other) const {
        return (conn_ != other.conn_) || (service_name_ != other.service_name_)
            || (object_name_ != other.object_name_);
    }

    template<typename ... TInputs, typename ... TOutputs>
    int method_call_async(const char* interface_name, const char* method_name, Callback<TOutputs...>* callback, TInputs ... inputs) {
        DBusMessage* msg;
        DBusMessageIter args;
        DBusPendingCall* pending;

        auto pending_call_handler = [](DBusPendingCall* pending, void* ctx){
            Callback<TOutputs...>* callback = reinterpret_cast<Callback<TOutputs...>*>(ctx);
            DBusMessage* msg = dbus_pending_call_steal_reply(pending);
            dbus_pending_call_unref(pending);
            handle_reply_message(msg, callback);
            dbus_message_unref(msg);
        };

        // TODO: we get a segfault if we try to use a service name which does
        // not include a dot. Find out if this is libdbus' or our fault and
        // possibly file a bug report.

        msg = dbus_message_new_method_call(service_name_.c_str(), // target for the method call
                object_name_.c_str(), // object to call on
                interface_name, // interface to call on
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

        if (!dbus_pending_call_set_notify(pending, pending_call_handler, callback, nullptr)) {
            FIBRE_LOG(E) << "failed to set pending call callback";
            goto fail1;
        }

        // Handle reply now if it already arrived before we set the notify callback.
        msg = dbus_pending_call_steal_reply(pending);
        if (msg) {
            dbus_pending_call_unref(pending);
            handle_reply_message(msg, callback);
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

    DBusRemoteSignal& operator+=(Callback<TArgs...>* callback) {
        callbacks_.push_back(callback);
        if (callbacks_.size() == 1) {
            activate_filter(); // TODO: error handling
        }
        return *this;
    }

    DBusRemoteSignal& operator-=(Callback<TArgs...>* callback) {
        callbacks_.erase(std::remove(callbacks_.begin(), callbacks_.end(), callback), callbacks_.end());
        if (callbacks_.size() == 0) {
            deactivate_filter();
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
                    && (strcmp(dbus_message_get_path(message), self->parent_->object_name_.c_str()) == 0);

            if (matches) {
                handle_signal_message(message, self->callbacks_); // TODO: error handling
                return DBUS_HANDLER_RESULT_HANDLED;
            } else {
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    };

    int activate_filter() {
        if (!parent_ || !parent_->conn_) {
            FIBRE_LOG(E) << "object not initialized properly";
            return -1;
        }

        if (!dbus_connection_add_filter(parent_->conn_->get_libdbus_ptr(), filter_callback, this, nullptr)) {
            FIBRE_LOG(E) << "failed to add filter";
            return -1;
        }

        std::string rule = "type='signal',"
            "sender='" + parent_->service_name_ + "',"
            "interface='" + TInterface::get_interface_name() + "',"
            "member='" + std::string(name_) + "',"
            "path='" + parent_->object_name_ + "'";
        
        FIBRE_LOG(D) << "adding rule " << rule << " to connection";
        dbus_bus_add_match(parent_->conn_->get_libdbus_ptr(), rule.c_str(), nullptr);
        dbus_connection_flush(parent_->conn_->get_libdbus_ptr());
        return 0;
    }

    void deactivate_filter() {
        dbus_connection_remove_filter(parent_->conn_->get_libdbus_ptr(), filter_callback, this);
    }

    TInterface* parent_;
    const char* name_;
    std::vector<Callback<TArgs...>*> callbacks_;
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

template<size_t BITS, bool SIGNED> constexpr sstring<1> get_int_signature();
template<> constexpr sstring<1> get_int_signature<16, true>() { return DBUS_TYPE_INT16_AS_STRING; }
template<> constexpr sstring<1> get_int_signature<32, true>() { return DBUS_TYPE_INT32_AS_STRING; }
template<> constexpr sstring<1> get_int_signature<64, true>() { return DBUS_TYPE_INT64_AS_STRING; }
template<> constexpr sstring<1> get_int_signature<8, false>() { return DBUS_TYPE_BYTE_AS_STRING; }
template<> constexpr sstring<1> get_int_signature<16, false>() { return DBUS_TYPE_UINT16_AS_STRING; }
template<> constexpr sstring<1> get_int_signature<32, false>() { return DBUS_TYPE_UINT32_AS_STRING; }
template<> constexpr sstring<1> get_int_signature<64, false>() { return DBUS_TYPE_UINT64_AS_STRING; }


template<typename T>
static inline const char* get_signature() {
    static auto sig = DBusTypeTraits<T>::signature;
    return sig.c_str();
}

template<typename T>
struct DBusTypeTraits<T, typename std::is_integral<T>::type> {
    static constexpr size_t BITS = sizeof(T) * CHAR_BIT;
    using type_id = std::integral_constant<int, get_int_type_id<BITS, std::is_signed<T>::value>()>;
    static constexpr auto signature = get_int_signature<BITS, std::is_signed<T>::value>();

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
    static constexpr auto signature = make_sstring(DBUS_TYPE_BOOLEAN_AS_STRING);

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
    static constexpr sstring<1> signature = DBUS_TYPE_STRING_AS_STRING;

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
    static constexpr auto signature = make_sstring(DBUS_TYPE_ARRAY_AS_STRING) +
                                      DBusTypeTraits<TElement>::signature;

    static int push(DBusMessageIter* iter, std::vector<TElement> val) {
        DBusMessageIter subiter;
        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, get_signature<TElement>(), &subiter)) {
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
            make_sstring(DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING) +
            DBusTypeTraits<TKey>::signature + DBusTypeTraits<TVal>::signature +
            make_sstring(DBUS_DICT_ENTRY_END_CHAR_AS_STRING);
    static constexpr auto signature = make_sstring(DBUS_TYPE_ARRAY_AS_STRING) +
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
struct DBusTypeTraits<DBusObject> {
    using type_id = std::integral_constant<int, DBUS_TYPE_OBJECT_PATH>;
    static constexpr sstring<1> signature = DBUS_TYPE_OBJECT_PATH_AS_STRING;

    static int push(DBusMessageIter* iter, DBusObject val) {
        const char * object_path = val.object_name_.c_str();
        return dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &object_path) ? 0 : -1;
    }

    static int pop(DBusMessageIter* iter, DBusObject& val) {
        std::string object_path;
        int result = DBusTypeTraits<std::string>::pop(iter, object_path);
        if (result == 0) {
            val = DBusObject(nullptr, "", object_path);
        }
        return result;
    }
};

/** @brief Helper class for pushing and popping variants */
template<size_t I, size_t N, typename ... Ts>
struct variant_helper<I, N, std::variant<Ts...>> {
    using subtype = std::tuple_element_t<I, std::tuple<Ts...>>;
    using next_helper = variant_helper<(I+1), N, std::variant<Ts...>>;

    static const char* get_element_signature(std::variant<Ts...>& val) {
        if (val.index() == I) {
            return get_signature<subtype>();
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
        if (strcmp(get_signature<subtype>(), signature) == 0) {
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
    static constexpr sstring<1> signature = DBUS_TYPE_VARIANT_AS_STRING;

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


}


/* Extensions for std namespace ----------------------------------------------*/

namespace std {

template<typename ... Ts>
static std::ostream& operator<<(std::ostream& stream, const fibre::DBusObject& val) {
    return stream << "DBusObject(" << val.service_name_ << ", " << val.object_name_ << ")";
}

template<>
struct hash<fibre::DBusObject> {
    size_t operator()(const fibre::DBusObject& obj) const {
        return hash<uintptr_t>()(reinterpret_cast<uintptr_t>(obj.conn_)) +
               hash<string>()(obj.service_name_) +
               hash<string>()(obj.object_name_);
    }
};

}

#undef current_log_topic

#endif // __FIBRE_DBUS_HPP