#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_freedesktop_DBus_ObjectManager : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.freedesktop.DBus.ObjectManager";

    org_freedesktop_DBus_ObjectManager(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}

    int GetManagedObjects_async(fibre::Callback<std::unordered_map<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>>* callback) {
        return method_call_async(interface_name, "GetManagedObjects", callback);
    }

    // Callback<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>> InterfacesAdded;
    // Callback<DBusObject, std::vector<std::string>> InterfacesRemoved;
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP