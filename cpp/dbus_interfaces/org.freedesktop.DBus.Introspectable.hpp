#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_freedesktop_DBus_Introspectable : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.freedesktop.DBus.Introspectable";

    org_freedesktop_DBus_Introspectable(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}

    int Introspect_async(fibre::Callback<std::string>* callback) {
        return method_call_async(interface_name, "Introspect", callback);
    }

};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP