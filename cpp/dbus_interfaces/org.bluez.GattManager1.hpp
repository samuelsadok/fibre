#ifndef __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_GattManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.GattManager1";

    org_bluez_GattManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_GattManager1(const org_bluez_GattManager1 &) = delete;
    org_bluez_GattManager1& operator=(const org_bluez_GattManager1 &) = delete;


    int RegisterApplication_async(DBusObject application, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterApplication", application, options, callback);
    }

    int UnregisterApplication_async(DBusObject application, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterApplication", application, callback);
    }

};

#endif // __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP