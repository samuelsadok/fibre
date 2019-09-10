#ifndef __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class org_bluez_ProfileManager1 : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.bluez.ProfileManager1"; }

    org_bluez_ProfileManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_ProfileManager1(const org_bluez_ProfileManager1 &) = delete;
    org_bluez_ProfileManager1& operator=(const org_bluez_ProfileManager1 &) = delete;


    int RegisterProfile_async(DBusObject profile, std::string UUID, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "RegisterProfile", callback, profile, UUID, options);
    }

    int UnregisterProfile_async(DBusObject profile, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "UnregisterProfile", callback, profile);
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
};

#endif // __INTERFACES__ORG_BLUEZ_PROFILEMANAGER1_HPP