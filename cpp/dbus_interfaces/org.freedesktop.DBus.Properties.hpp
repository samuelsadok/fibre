#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_freedesktop_DBus_Properties : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.freedesktop.DBus.Properties";

    org_freedesktop_DBus_Properties(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_Properties(const org_freedesktop_DBus_Properties &) = delete;
    org_freedesktop_DBus_Properties& operator=(const org_freedesktop_DBus_Properties &) = delete;


    int Get_async(std::string interface, std::string name, fibre::Callback<fibre::dbus_variant>* callback) {
        return method_call_async(interface_name, "Get", interface, name, callback);
    }

    int Set_async(std::string interface, std::string name, fibre::dbus_variant value, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "Set", interface, name, value, callback);
    }

    int GetAll_async(std::string interface, fibre::Callback<std::unordered_map<std::string, fibre::dbus_variant>>* callback) {
        return method_call_async(interface_name, "GetAll", interface, callback);
    }

    fibre::DBusRemoteSignal<org_freedesktop_DBus_Properties, std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged{this, "PropertiesChanged"};
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP