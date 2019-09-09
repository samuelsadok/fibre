#ifndef __INTERFACES__ORG_BLUEZ_MEDIA1_HPP
#define __INTERFACES__ORG_BLUEZ_MEDIA1_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class org_bluez_Media1 : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.bluez.Media1"; }

    org_bluez_Media1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Media1(const org_bluez_Media1 &) = delete;
    org_bluez_Media1& operator=(const org_bluez_Media1 &) = delete;


    int RegisterEndpoint_async(DBusObject endpoint, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "RegisterEndpoint", callback, endpoint, properties);
    }

    int UnregisterEndpoint_async(DBusObject endpoint, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "UnregisterEndpoint", callback, endpoint);
    }

    int RegisterPlayer_async(DBusObject player, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "RegisterPlayer", callback, player, properties);
    }

    int UnregisterPlayer_async(DBusObject player, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "UnregisterPlayer", callback, player);
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
            (*this)["RegisterEndpoint"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<DBusObject, std::unordered_map<std::string, fibre::dbus_variant>>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::RegisterEndpoint>((TImpl*)obj)); }});
            (*this)["UnregisterEndpoint"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<DBusObject>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::UnregisterEndpoint>((TImpl*)obj)); }});
            (*this)["RegisterPlayer"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<DBusObject, std::unordered_map<std::string, fibre::dbus_variant>>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::RegisterPlayer>((TImpl*)obj)); }});
            (*this)["UnregisterPlayer"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<DBusObject>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::UnregisterPlayer>((TImpl*)obj)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__ORG_BLUEZ_MEDIA1_HPP