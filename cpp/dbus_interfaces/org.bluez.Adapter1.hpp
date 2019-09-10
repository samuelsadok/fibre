#ifndef __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP
#define __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class org_bluez_Adapter1 : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.bluez.Adapter1"; }

    org_bluez_Adapter1(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Adapter1(const org_bluez_Adapter1 &) = delete;
    org_bluez_Adapter1& operator=(const org_bluez_Adapter1 &) = delete;


    int StartDiscovery_async(fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "StartDiscovery", callback);
    }

    int SetDiscoveryFilter_async(std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "SetDiscoveryFilter", callback, properties);
    }

    int StopDiscovery_async(fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "StopDiscovery", callback);
    }

    int RemoveDevice_async(DBusObject device, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "RemoveDevice", callback, device);
    }

    int GetDiscoveryFilters_async(fibre::Callback<std::vector<std::string>>* callback) {
        return method_call_async(get_interface_name(), "GetDiscoveryFilters", callback);
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

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["StartDiscovery"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::StartDiscovery, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["SetDiscoveryFilter"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::SetDiscoveryFilter, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["StopDiscovery"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::StopDiscovery, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["RemoveDevice"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RemoveDevice, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            (*this)["GetDiscoveryFilters"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::GetDiscoveryFilters, (TImpl*)obj, (std::tuple<std::vector<std::string>>*)nullptr)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__ORG_BLUEZ_ADAPTER1_HPP