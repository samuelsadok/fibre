#ifndef __INTERFACES__ORG_BLUEZ_NETWORKSERVER1_HPP
#define __INTERFACES__ORG_BLUEZ_NETWORKSERVER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_NetworkServer1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.NetworkServer1";

    org_bluez_NetworkServer1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}

    int Register_async(std::string uuid, std::string bridge, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "Register", uuid, bridge, callback);
    }

    int Unregister_async(std::string uuid, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "Unregister", uuid, callback);
    }

};

#endif // __INTERFACES__ORG_BLUEZ_NETWORKSERVER1_HPP