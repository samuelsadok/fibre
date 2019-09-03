#ifndef __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <vector>


class org_bluez_AgentManager1 : public fibre::DBusObject {
public:
    static constexpr const char* interface_name = "org.bluez.AgentManager1";

    org_bluez_AgentManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_AgentManager1(const org_bluez_AgentManager1 &) = delete;
    org_bluez_AgentManager1& operator=(const org_bluez_AgentManager1 &) = delete;


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