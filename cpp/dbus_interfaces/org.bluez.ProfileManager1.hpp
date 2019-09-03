#ifndef __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_ProfileManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.ProfileManager1";

    org_bluez_ProfileManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_ProfileManager1(const org_bluez_ProfileManager1 &) = delete;
    org_bluez_ProfileManager1& operator=(const org_bluez_ProfileManager1 &) = delete;


    int RegisterProfile_async(DBusObject profile, std::string UUID, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterProfile", profile, UUID, options, callback);
    }

    int UnregisterProfile_async(DBusObject profile, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterProfile", profile, callback);
    }

};

#endif // __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP