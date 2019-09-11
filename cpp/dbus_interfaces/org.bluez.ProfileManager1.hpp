#ifndef __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_ProfileManager1 {
public:
    static const char* get_interface_name() { return "org.bluez.ProfileManager1"; }

    org_bluez_ProfileManager1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_ProfileManager1(const org_bluez_ProfileManager1 &) = delete;
    org_bluez_ProfileManager1& operator=(const org_bluez_ProfileManager1 &) = delete;


    int RegisterProfile_async(fibre::DBusObjectPath profile, std::string UUID, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<org_bluez_ProfileManager1*>* callback) {
        return base_->method_call_async(this, "RegisterProfile", callback, profile, UUID, options);
    }

    int UnregisterProfile_async(fibre::DBusObjectPath profile, fibre::Callback<org_bluez_ProfileManager1*>* callback) {
        return base_->method_call_async(this, "UnregisterProfile", callback, profile);
    }


    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "RegisterProfile", fibre::FunctionImplTable{} },
            { "UnregisterProfile", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["RegisterProfile"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterProfile, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["UnregisterProfile"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterProfile, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP