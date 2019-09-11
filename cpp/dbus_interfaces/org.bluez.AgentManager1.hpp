#ifndef __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_AgentManager1 {
public:
    static const char* get_interface_name() { return "org.bluez.AgentManager1"; }

    org_bluez_AgentManager1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_AgentManager1(const org_bluez_AgentManager1 &) = delete;
    org_bluez_AgentManager1& operator=(const org_bluez_AgentManager1 &) = delete;


    int RegisterAgent_async(fibre::DBusObjectPath agent, std::string capability, fibre::Callback<org_bluez_AgentManager1*>* callback) {
        return base_->method_call_async(this, "RegisterAgent", callback, agent, capability);
    }

    int UnregisterAgent_async(fibre::DBusObjectPath agent, fibre::Callback<org_bluez_AgentManager1*>* callback) {
        return base_->method_call_async(this, "UnregisterAgent", callback, agent);
    }

    int RequestDefaultAgent_async(fibre::DBusObjectPath agent, fibre::Callback<org_bluez_AgentManager1*>* callback) {
        return base_->method_call_async(this, "RequestDefaultAgent", callback, agent);
    }


    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "RegisterAgent", fibre::FunctionImplTable{} },
            { "UnregisterAgent", fibre::FunctionImplTable{} },
            { "RequestDefaultAgent", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["RegisterAgent"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterAgent, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["UnregisterAgent"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterAgent, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["RequestDefaultAgent"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RequestDefaultAgent, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_AGENTMANAGER1_HPP