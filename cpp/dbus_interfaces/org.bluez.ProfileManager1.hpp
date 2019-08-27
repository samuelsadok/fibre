#ifndef __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_ProfileManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.ProfileManager1";

    org_bluez_ProfileManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}

    int RegisterProfile_async(DBusObject profile, std::string UUID, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterProfile", profile, UUID, options, callback);
    }

    int UnregisterProfile_async(DBusObject profile, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterProfile", profile, callback);
    }

};

#endif // __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP