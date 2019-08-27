#ifndef __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_LEAdvertisingManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.LEAdvertisingManager1";

    org_bluez_LEAdvertisingManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}

    int RegisterAdvertisement_async(DBusObject advertisement, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterAdvertisement", advertisement, options, callback);
    }

    int UnregisterAdvertisement_async(DBusObject service, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterAdvertisement", service, callback);
    }

    // DBusProperty<uint8_t> ActiveInstances;
    // DBusProperty<uint8_t> SupportedInstances;
    // DBusProperty<std::vector<std::string>> SupportedIncludes;
};

#endif // __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP