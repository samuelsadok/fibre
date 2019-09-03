#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_freedesktop_DBus_Introspectable : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.freedesktop.DBus.Introspectable";

    org_freedesktop_DBus_Introspectable(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_Introspectable(const org_freedesktop_DBus_Introspectable &) = delete;
    org_freedesktop_DBus_Introspectable& operator=(const org_freedesktop_DBus_Introspectable &) = delete;


    int Introspect_async(fibre::Callback<std::string>* callback) {
        return method_call_async(interface_name, "Introspect", callback);
    }

};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP