#ifndef __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP
#define __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP

#include <fibre/platform_support/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_Adapter1 {
public:
    struct tag {};
    static const char* get_interface_name() { return "org.bluez.Adapter1"; }

    org_bluez_Adapter1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Adapter1(const org_bluez_Adapter1 &) = delete;
    org_bluez_Adapter1& operator=(const org_bluez_Adapter1 &) = delete;


    int StartDiscovery_async(fibre::Callback<org_bluez_Adapter1*>* callback) {
        return base_->method_call_async(this, "StartDiscovery", callback);
    }

    int SetDiscoveryFilter_async(std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<org_bluez_Adapter1*>* callback) {
        return base_->method_call_async(this, "SetDiscoveryFilter", callback, properties);
    }

    int StopDiscovery_async(fibre::Callback<org_bluez_Adapter1*>* callback) {
        return base_->method_call_async(this, "StopDiscovery", callback);
    }

    int RemoveDevice_async(fibre::DBusObjectPath device, fibre::Callback<org_bluez_Adapter1*>* callback) {
        return base_->method_call_async(this, "RemoveDevice", callback, device);
    }

    int GetDiscoveryFilters_async(fibre::Callback<org_bluez_Adapter1*, std::vector<std::string>>* callback) {
        return base_->method_call_async(this, "GetDiscoveryFilters", callback);
    }

    // DBusProperty<std::string> Address;
    // DBusProperty<std::string> AddressType;
    // DBusProperty<std::string> Name;
    // DBusProperty<std::string> Alias;
    // DBusProperty<uint32_t> Class;
    // DBusProperty<bool> Powered;
    // DBusProperty<bool> Discoverable;
    // DBusProperty<uint32_t> DiscoverableTimeout;
    // DBusProperty<bool> Pairable;
    // DBusProperty<uint32_t> PairableTimeout;
    // DBusProperty<bool> Discovering;
    // DBusProperty<std::vector<std::string>> UUIDs;
    // DBusProperty<std::string> Modalias;

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "StartDiscovery", fibre::FunctionImplTable{} },
            { "SetDiscoveryFilter", fibre::FunctionImplTable{} },
            { "StopDiscovery", fibre::FunctionImplTable{} },
            { "RemoveDevice", fibre::FunctionImplTable{} },
            { "GetDiscoveryFilters", fibre::FunctionImplTable{} },
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["StartDiscovery"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::StartDiscovery, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["SetDiscoveryFilter"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::SetDiscoveryFilter, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["StopDiscovery"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::StopDiscovery, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["RemoveDevice"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RemoveDevice, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["GetDiscoveryFilters"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::GetDiscoveryFilters, (TImpl*)obj, (std::tuple<std::vector<std::string>>*)nullptr)); }});
            }
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["StartDiscovery"].erase((*this)["StartDiscovery"].find(type_id));
                (*this)["SetDiscoveryFilter"].erase((*this)["SetDiscoveryFilter"].find(type_id));
                (*this)["StopDiscovery"].erase((*this)["StopDiscovery"].find(type_id));
                (*this)["RemoveDevice"].erase((*this)["RemoveDevice"].find(type_id));
                (*this)["GetDiscoveryFilters"].erase((*this)["GetDiscoveryFilters"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP