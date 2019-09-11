#ifndef __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP
#define __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_LEAdvertisingManager1 {
public:
    static const char* get_interface_name() { return "org.bluez.LEAdvertisingManager1"; }

    org_bluez_LEAdvertisingManager1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_LEAdvertisingManager1(const org_bluez_LEAdvertisingManager1 &) = delete;
    org_bluez_LEAdvertisingManager1& operator=(const org_bluez_LEAdvertisingManager1 &) = delete;


    int RegisterAdvertisement_async(fibre::DBusObjectPath advertisement, std::unordered_map<std::string, fibre::dbus_variant> options, fibre::Callback<org_bluez_LEAdvertisingManager1*>* callback) {
        return base_->method_call_async(this, "RegisterAdvertisement", callback, advertisement, options);
    }

    int UnregisterAdvertisement_async(fibre::DBusObjectPath service, fibre::Callback<org_bluez_LEAdvertisingManager1*>* callback) {
        return base_->method_call_async(this, "UnregisterAdvertisement", callback, service);
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
            (*this)["RegisterAdvertisement"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterAdvertisement, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["UnregisterAdvertisement"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterAdvertisement, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_LEADVERTISINGMANAGER1_HPP