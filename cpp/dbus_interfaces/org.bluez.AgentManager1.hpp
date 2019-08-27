#ifndef __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_AgentManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.AgentManager1";

    org_bluez_AgentManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}

    int RegisterAgent_async(DBusObject agent, std::string capability, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RegisterAgent", agent, capability, callback);
    }

    int UnregisterAgent_async(DBusObject agent, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "UnregisterAgent", agent, callback);
    }

    int RequestDefaultAgent_async(DBusObject agent, fibre::Callback<>* callback) {
        return method_call_async(interface_name, "RequestDefaultAgent", agent, callback);
    }

};

#endif // __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP