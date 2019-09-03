#ifndef __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_LEAdvertisingManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.LEAdvertisingManager1";

    org_bluez_LEAdvertisingManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_LEAdvertisingManager1(const org_bluez_LEAdvertisingManager1 &) = delete;
    org_bluez_LEAdvertisingManager1& operator=(const org_bluez_LEAdvertisingManager1 &) = delete;


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