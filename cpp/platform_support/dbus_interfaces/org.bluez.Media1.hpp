#ifndef __INTERFACES__ORG_BLUEZ_MEDIA1_HPP
#define __INTERFACES__ORG_BLUEZ_MEDIA1_HPP

#include <fibre/platform_support/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_Media1 {
public:
    struct tag {};
    static const char* get_interface_name() { return "org.bluez.Media1"; }

    org_bluez_Media1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_Media1(const org_bluez_Media1 &) = delete;
    org_bluez_Media1& operator=(const org_bluez_Media1 &) = delete;


    int RegisterEndpoint_async(fibre::DBusObjectPath endpoint, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<org_bluez_Media1*>* callback, fibre::Callback<org_bluez_Media1*>* failed_callback) {
        return base_->method_call_async(this, "RegisterEndpoint", callback, failed_callback, endpoint, properties);
    }

    int UnregisterEndpoint_async(fibre::DBusObjectPath endpoint, fibre::Callback<org_bluez_Media1*>* callback, fibre::Callback<org_bluez_Media1*>* failed_callback) {
        return base_->method_call_async(this, "UnregisterEndpoint", callback, failed_callback, endpoint);
    }

    int RegisterPlayer_async(fibre::DBusObjectPath player, std::unordered_map<std::string, fibre::dbus_variant> properties, fibre::Callback<org_bluez_Media1*>* callback, fibre::Callback<org_bluez_Media1*>* failed_callback) {
        return base_->method_call_async(this, "RegisterPlayer", callback, failed_callback, player, properties);
    }

    int UnregisterPlayer_async(fibre::DBusObjectPath player, fibre::Callback<org_bluez_Media1*>* callback, fibre::Callback<org_bluez_Media1*>* failed_callback) {
        return base_->method_call_async(this, "UnregisterPlayer", callback, failed_callback, player);
    }


    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "RegisterEndpoint", fibre::FunctionImplTable{} },
            { "UnregisterEndpoint", fibre::FunctionImplTable{} },
            { "RegisterPlayer", fibre::FunctionImplTable{} },
            { "UnregisterPlayer", fibre::FunctionImplTable{} },
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["RegisterEndpoint"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterEndpoint, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["UnregisterEndpoint"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterEndpoint, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["RegisterPlayer"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::RegisterPlayer, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["UnregisterPlayer"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::UnregisterPlayer, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
            }
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["RegisterEndpoint"].erase((*this)["RegisterEndpoint"].find(type_id));
                (*this)["UnregisterEndpoint"].erase((*this)["UnregisterEndpoint"].find(type_id));
                (*this)["RegisterPlayer"].erase((*this)["RegisterPlayer"].find(type_id));
                (*this)["UnregisterPlayer"].erase((*this)["UnregisterPlayer"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_MEDIA1_HPP