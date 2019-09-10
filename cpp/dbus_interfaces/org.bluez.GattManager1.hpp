#ifndef __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_GattManager1 : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.bluez.GattManager1"; }

    org_bluez_GattManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_GattManager1(const org_bluez_GattManager1 &) = delete;
    org_bluez_GattManager1& operator=(const org_bluez_GattManager1 &) = delete;


    int RegisterApplication_async(DBusObject application, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "RegisterApplication", callback, application, options);
    }

    int UnregisterApplication_async(DBusObject application, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "UnregisterApplication", callback, application);
    }


    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "RegisterApplication", fibre::FunctionImplTable{} },
            { "UnregisterApplication", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["RegisterApplication"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterApplication, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["UnregisterApplication"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterApplication, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP