#ifndef __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP
#define __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_Adapter1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.Adapter1";

    org_bluez_Adapter1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Adapter1(const org_bluez_Adapter1 &) = delete;
    org_bluez_Adapter1& operator=(const org_bluez_Adapter1 &) = delete;


    int StartDiscovery_async(fibre::Callback<>* callback) {
        return method_call_async(interface_name, "StartDiscovery", callback);
    }

    int SetDiscoveryFilter_async(std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "SetDiscoveryFilter", properties, callback);
    }

    int StopDiscovery_async(fibre::Callback<>* callback) {
        return method_call_async(interface_name, "StopDiscovery", callback);
    }

    int RemoveDevice_async(DBusObject device, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RemoveDevice", device, callback);
    }

    int GetDiscoveryFilters_async(fibre::Callback<std::vector<std::string>>* callback) {
        return method_call_async(interface_name, "GetDiscoveryFilters", callback);
    }

    // DBusProperty<std::string> Address;
    // DBusProperty<std::string> AddressType;
    // DBusProperty<std::string> Name;
    // DBusProperty<std::string> Alias;
    // DBusProperty<uint32_t> Class;
    // DBusProperty<bool> Powered;
    // DBusProperty<bool> Discoverable;
    // DBusProperty<uint32_t> DiscoverableTimeout;
    // DBusProperty<bool> Pairable;
    // DBusProperty<uint32_t> PairableTimeout;
    // DBusProperty<bool> Discovering;
    // DBusProperty<std::vector<std::string>> UUIDs;
    // DBusProperty<std::string> Modalias;
};

#endif // __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP