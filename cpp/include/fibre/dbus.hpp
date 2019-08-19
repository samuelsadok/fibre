#ifndef __FIBRE_USB_DBUS_HPP
#define __FIBRE_USB_DBUS_HPP

// helpful reference: http://www.matthew.ath.cx/misc/dbus

#include <dbus/dbus.h>
#include <unordered_map>
#include <fibre/worker.hpp>

namespace fibre {

class dbus_variant;

void append_object(DBusMessageIter* iter, const char* str);
void append_object(DBusMessageIter* iter, dbus_variant& val);
void append_object(DBusMessageIter* iter, std::unordered_map<const char*, const char*> val);

class dbus_variant {
public:
    //template <typename T> using fn_type = (void (*)(DBusMessageIter*, T));

    template<typename T>
    void load_append_function(T& value) {
        //using fn_type = void (*)(DBusMessageIter*, T);
        //fn_type<T> fn = fn_type<T>{append_object};
        val = &value;
        append_variant_fn = [](DBusMessageIter* iter, const void* val) {
            append_object(iter, (T*)val);
        };
    }

    explicit dbus_variant(const char* str) {
        type_as_string = DBUS_TYPE_STRING_AS_STRING;
        type = DBUS_TYPE_STRING;
        load_append_function(*str);
    }

    const char* get_type_as_string() { return type_as_string; }
    int get_type() { return type; }
    void append_variant(DBusMessageIter* iter) {
        return append_variant_fn(iter, val);
    }

    const char* type_as_string;
    int type;
    const void* val;
    void (*append_variant_fn)(DBusMessageIter* iter, const void* val) = nullptr;
};

/*template<typename T>
class dbus_variant {
protected:
    append_object()
};*/


//void append_object();




class DBusConnectionWrapper {
public:
    int init(Worker* worker);
    int deinit();

    DBusConnection* get_libdbus_ptr() { return conn_; }

private:
    int handle_add_watch(DBusWatch *watch);
    void handle_remove_watch(DBusWatch *watch);
    int handle_toggle_watch(DBusWatch *watch, bool enable);

    int handle_add_timeout(DBusTimeout* timeout);
    void handle_remove_timeout(DBusTimeout* timeout);
    int handle_toggle_timeout(DBusTimeout *timeout, bool enable);

    DBusError err_;
    DBusConnection* conn_;
    Worker* worker_;

    Signal dispatch_signal_ = Signal("dbus dispatch");
    Signal::callback_t dispatch_callback_obj_ = {
        .callback = [](void* ctx){ printf("dispatch\n"); dbus_connection_dispatch(((DBusConnectionWrapper*)ctx)->conn_); },
        .ctx = this
    };
};


class DBusObject {
public:
    DBusObject(DBusConnectionWrapper* conn, const char* service_name, const char* object_name) 
        : conn(conn), service_name(service_name), object_name(object_name) {}
    DBusConnectionWrapper* conn;
    const char* service_name;
    const char* object_name;
};


int get_managed_objects_async(DBusObject* obj, Callback<const char*>* callback);

}

#endif // __FIBRE_USB_DBUS_HPP