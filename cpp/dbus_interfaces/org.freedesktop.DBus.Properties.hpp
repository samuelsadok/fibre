#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class org_freedesktop_DBus_Properties : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.freedesktop.DBus.Properties"; }

    org_freedesktop_DBus_Properties(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_Properties(const org_freedesktop_DBus_Properties &) = delete;
    org_freedesktop_DBus_Properties& operator=(const org_freedesktop_DBus_Properties &) = delete;


    int Get_async(std::string interface, std::string name, fibre::Callback<fibre::dbus_variant>* callback) {
        return method_call_async(get_interface_name(), "Get", callback, interface, name);
    }

    int Set_async(std::string interface, std::string name, fibre::dbus_variant value, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "Set", callback, interface, name, value);
    }

    int GetAll_async(std::string interface, fibre::Callback<std::unordered_map<std::string, fibre::dbus_variant>>* callback) {
        return method_call_async(get_interface_name(), "GetAll", callback, interface);
    }

    fibre::DBusRemoteSignal<org_freedesktop_DBus_Properties, std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged{this, "PropertiesChanged"};

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "Get", fibre::FunctionImplTable{} },
            { "Set", fibre::FunctionImplTable{} },
            { "GetAll", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["Get"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<std::string, std::string>, std::tuple<fibre::dbus_variant>>::template from_member_fn<TImpl, &TImpl::Get>((TImpl*)obj)); }});
            (*this)["Set"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<std::string, std::string, fibre::dbus_variant>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::Set>((TImpl*)obj)); }});
            (*this)["GetAll"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<std::string>, std::tuple<std::unordered_map<std::string, fibre::dbus_variant>>>::template from_member_fn<TImpl, &TImpl::GetAll>((TImpl*)obj)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP