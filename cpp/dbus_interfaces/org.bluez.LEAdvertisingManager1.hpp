#ifndef __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class org_bluez_LEAdvertisingManager1 : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.bluez.LEAdvertisingManager1"; }

    org_bluez_LEAdvertisingManager1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_LEAdvertisingManager1(const org_bluez_LEAdvertisingManager1 &) = delete;
    org_bluez_LEAdvertisingManager1& operator=(const org_bluez_LEAdvertisingManager1 &) = delete;


    int RegisterAdvertisement_async(DBusObject advertisement, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "RegisterAdvertisement", callback, advertisement, options);
    }

    int UnregisterAdvertisement_async(DBusObject service, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "UnregisterAdvertisement", callback, service);
    }

    // DBusProperty<uint8_t> ActiveInstances;
    // DBusProperty<uint8_t> SupportedInstances;
    // DBusProperty<std::vector<std::string>> SupportedIncludes;

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "RegisterAdvertisement", fibre::FunctionImplTable{} },
            { "UnregisterAdvertisement", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["RegisterAdvertisement"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<DBusObject, std::unordered_map<std::string, fibre::dbus_variant>>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::RegisterAdvertisement>((TImpl*)obj)); }});
            (*this)["UnregisterAdvertisement"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<DBusObject>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::UnregisterAdvertisement>((TImpl*)obj)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP