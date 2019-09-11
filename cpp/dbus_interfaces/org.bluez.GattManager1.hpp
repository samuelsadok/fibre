#ifndef __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_GattManager1 {
public:
    static const char* get_interface_name() { return "org.bluez.GattManager1"; }

    org_bluez_GattManager1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_GattManager1(const org_bluez_GattManager1 &) = delete;
    org_bluez_GattManager1& operator=(const org_bluez_GattManager1 &) = delete;


    int RegisterApplication_async(fibre::DBusObjectPath application, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<org_bluez_GattManager1*>* callback) {
        return base_->method_call_async(this, "RegisterApplication", callback, application, options);
    }

    int UnregisterApplication_async(fibre::DBusObjectPath application, fibre::Callback<org_bluez_GattManager1*>* callback) {
        return base_->method_call_async(this, "UnregisterApplication", callback, application);
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

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_GATTMANAGER1_HPP