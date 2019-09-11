#ifndef __INTERFACES__ORG_BLUEZ_LEADVERTISEMENT1_HPP
#define __INTERFACES__ORG_BLUEZ_LEADVERTISEMENT1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_LEAdvertisement1 {
public:
    static const char* get_interface_name() { return "org.bluez.LEAdvertisement1"; }

    org_bluez_LEAdvertisement1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_LEAdvertisement1(const org_bluez_LEAdvertisement1 &) = delete;
    org_bluez_LEAdvertisement1& operator=(const org_bluez_LEAdvertisement1 &) = delete;


    int Release_async(fibre::Callback<org_bluez_LEAdvertisement1*>* callback) {
        return base_->method_call_async(this, "Release", callback);
    }

    // DBusProperty<std::string> Type;
    // DBusProperty<std::vector<std::string>> ServiceUUIDs;
    // DBusProperty<std::unordered_map<uint16_t, fibre::dbus_variant>> ManufacturerData;
    // DBusProperty<std::vector<std::string>> SolicitUUIDs;
    // DBusProperty<std::vector<std::string>> Includes;
    // DBusProperty<std::unordered_map<std::string, fibre::dbus_variant>> ServiceData;
    // DBusProperty<bool> IncludeTxPower;
    // DBusProperty<std::string> LocalName;
    // DBusProperty<uint16_t> Appearance;
    // DBusProperty<uint16_t> Duration;
    // DBusProperty<uint16_t> Timeout;

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "Release", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["Release"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Release, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_LEADVERTISEMENT1_HPP