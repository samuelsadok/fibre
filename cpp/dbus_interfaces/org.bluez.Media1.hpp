#ifndef __INTERFACES__ORG_BLUEZ_MEDIA1_HPP
#define __INTERFACES__ORG_BLUEZ_MEDIA1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_Media1 {
public:
    static const char* get_interface_name() { return "org.bluez.Media1"; }

    org_bluez_Media1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Media1(const org_bluez_Media1 &) = delete;
    org_bluez_Media1& operator=(const org_bluez_Media1 &) = delete;


    int RegisterEndpoint_async(fibre::DBusObjectPath endpoint, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<org_bluez_Media1*>* callback) {
        return base_->method_call_async(this, "RegisterEndpoint", callback, endpoint, properties);
    }

    int UnregisterEndpoint_async(fibre::DBusObjectPath endpoint, fibre::Callback<org_bluez_Media1*>* callback) {
        return base_->method_call_async(this, "UnregisterEndpoint", callback, endpoint);
    }

    int RegisterPlayer_async(fibre::DBusObjectPath player, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<org_bluez_Media1*>* callback) {
        return base_->method_call_async(this, "RegisterPlayer", callback, player, properties);
    }

    int UnregisterPlayer_async(fibre::DBusObjectPath player, fibre::Callback<org_bluez_Media1*>* callback) {
        return base_->method_call_async(this, "UnregisterPlayer", callback, player);
    }


    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "RegisterEndpoint", fibre::FunctionImplTable{} },
            { "UnregisterEndpoint", fibre::FunctionImplTable{} },
            { "RegisterPlayer", fibre::FunctionImplTable{} },
            { "UnregisterPlayer", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["RegisterEndpoint"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterEndpoint, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["UnregisterEndpoint"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterEndpoint, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["RegisterPlayer"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterPlayer, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["UnregisterPlayer"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterPlayer, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_MEDIA1_HPP