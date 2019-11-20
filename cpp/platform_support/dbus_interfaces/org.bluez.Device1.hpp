#ifndef __INTERFACES__ORG_BLUEZ_DEVICE1_HPP
#define __INTERFACES__ORG_BLUEZ_DEVICE1_HPP

#include <fibre/platform_support/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_Device1 {
public:
    struct tag {};
    static const char* get_interface_name() { return "org.bluez.Device1"; }

    org_bluez_Device1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Device1(const org_bluez_Device1 &) = delete;
    org_bluez_Device1& operator=(const org_bluez_Device1 &) = delete;


    int Disconnect_async(fibre::Callback<org_bluez_Device1*>* callback, fibre::Callback<org_bluez_Device1*>* failed_callback) {
        return base_->method_call_async(this, "Disconnect", callback, failed_callback);
    }

    int Connect_async(fibre::Callback<org_bluez_Device1*>* callback, fibre::Callback<org_bluez_Device1*>* failed_callback) {
        return base_->method_call_async(this, "Connect", callback, failed_callback);
    }

    int ConnectProfile_async(std::string UUID, fibre::Callback<org_bluez_Device1*>* callback, fibre::Callback<org_bluez_Device1*>* failed_callback) {
        return base_->method_call_async(this, "ConnectProfile", callback, failed_callback, UUID);
    }

    int DisconnectProfile_async(std::string UUID, fibre::Callback<org_bluez_Device1*>* callback, fibre::Callback<org_bluez_Device1*>* failed_callback) {
        return base_->method_call_async(this, "DisconnectProfile", callback, failed_callback, UUID);
    }

    int Pair_async(fibre::Callback<org_bluez_Device1*>* callback, fibre::Callback<org_bluez_Device1*>* failed_callback) {
        return base_->method_call_async(this, "Pair", callback, failed_callback);
    }

    int CancelPairing_async(fibre::Callback<org_bluez_Device1*>* callback, fibre::Callback<org_bluez_Device1*>* failed_callback) {
        return base_->method_call_async(this, "CancelPairing", callback, failed_callback);
    }

    // DBusProperty<std::string> Address;
    // DBusProperty<std::string> Name;
    // DBusProperty<std::string> Alias;
    // DBusProperty<uint32_t> Class;
    // DBusProperty<uint16_t> Appearance;
    // DBusProperty<std::string> Icon;
    // DBusProperty<bool> Paired;
    // DBusProperty<bool> Trusted;
    // DBusProperty<bool> Blocked;
    // DBusProperty<bool> LegacyPairing;
    // DBusProperty<int16_t> RSSI;
    // DBusProperty<bool> Connected;
    // DBusProperty<std::vector<std::string>> UUIDs;
    // DBusProperty<std::string> Modalias;
    // DBusProperty<fibre::DBusObjectPath> Adapter;
    // DBusProperty<std::unordered_map<uint16_t, fibre::dbus_variant>> ManufacturerData;
    // DBusProperty<std::unordered_map<std::string, fibre::dbus_variant>> ServiceData;
    // DBusProperty<int16_t> TxPower;
    // DBusProperty<std::vector<fibre::DBusObjectPath>> GattServices;

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "Disconnect", fibre::FunctionImplTable{} },
            { "Connect", fibre::FunctionImplTable{} },
            { "ConnectProfile", fibre::FunctionImplTable{} },
            { "DisconnectProfile", fibre::FunctionImplTable{} },
            { "Pair", fibre::FunctionImplTable{} },
            { "CancelPairing", fibre::FunctionImplTable{} },
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["Disconnect"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Disconnect, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["Connect"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Connect, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["ConnectProfile"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::ConnectProfile, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["DisconnectProfile"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::DisconnectProfile, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["Pair"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Pair, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["CancelPairing"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::CancelPairing, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            }
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["Disconnect"].erase((*this)["Disconnect"].find(type_id));
                (*this)["Connect"].erase((*this)["Connect"].find(type_id));
                (*this)["ConnectProfile"].erase((*this)["ConnectProfile"].find(type_id));
                (*this)["DisconnectProfile"].erase((*this)["DisconnectProfile"].find(type_id));
                (*this)["Pair"].erase((*this)["Pair"].find(type_id));
                (*this)["CancelPairing"].erase((*this)["CancelPairing"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_DEVICE1_HPP