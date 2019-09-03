#ifndef __INTERFACES__ORG_BLUEZ_MEDIA1_HPP
#define __INTERFACES__ORG_BLUEZ_MEDIA1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_Media1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.Media1";

    org_bluez_Media1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Media1(const org_bluez_Media1 &) = delete;
    org_bluez_Media1& operator=(const org_bluez_Media1 &) = delete;


    int RegisterEndpoint_async(DBusObject endpoint, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterEndpoint", endpoint, properties, callback);
    }

    int UnregisterEndpoint_async(DBusObject endpoint, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterEndpoint", endpoint, callback);
    }

    int RegisterPlayer_async(DBusObject player, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterPlayer", player, properties, callback);
    }

    int UnregisterPlayer_async(DBusObject player, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterPlayer", player, callback);
    }

};

#endif // __INTERFACES__ORG_BLUEZ_MEDIA1_HPP