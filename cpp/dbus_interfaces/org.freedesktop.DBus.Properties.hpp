#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_freedesktop_DBus_Properties {
public:
    static const char* get_interface_name() { return "org.freedesktop.DBus.Properties"; }

    org_freedesktop_DBus_Properties(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_Properties(const org_freedesktop_DBus_Properties &) = delete;
    org_freedesktop_DBus_Properties& operator=(const org_freedesktop_DBus_Properties &) = delete;


    int Get_async(std::string interface, std::string name, fibre::Callback<org_freedesktop_DBus_Properties*, fibre::dbus_variant>* callback) {
        return base_->method_call_async(this, "Get", callback, interface, name);
    }

    int Set_async(std::string interface, std::string name, fibre::dbus_variant value, fibre::Callback<org_freedesktop_DBus_Properties*>* callback) {
        return base_->method_call_async(this, "Set", callback, interface, name, value);
    }

    int GetAll_async(std::string interface, fibre::Callback<org_freedesktop_DBus_Properties*, std::unordered_map<std::string, fibre::dbus_variant>>* callback) {
        return base_->method_call_async(this, "GetAll", callback, interface);
    }

    fibre::DBusRemoteSignal<org_freedesktop_DBus_Properties, std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged{this, "PropertiesChanged"};

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "Get", fibre::FunctionImplTable{} },
            { "Set", fibre::FunctionImplTable{} },
            { "GetAll", fibre::FunctionImplTable{} },
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;
        std::unordered_map<std::string, signal_table_entry_t<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>>> PropertiesChanged_callbacks{};

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["Get"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Get, (TImpl*)obj, (std::tuple<fibre::dbus_variant>*)nullptr)); }});
                (*this)["Set"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Set, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["GetAll"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::GetAll, (TImpl*)obj, (std::tuple<std::unordered_map<std::string, fibre::dbus_variant>>*)nullptr)); }});
            }
            obj.PropertiesChanged += &(PropertiesChanged_callbacks.insert({path + " @ " + conn.get_name(), {fibre::make_closure(&fibre::DBusConnectionWrapper::emit_signal<org_freedesktop_DBus_Properties, std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>>, &conn).bind(std::string("PropertiesChanged")).bind(path), [](void* ctx, signal_closure_t<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>>& cb){ ((TImpl*)ctx)->PropertiesChanged -= &cb; } } }).first->second.first);
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            {
                auto it = PropertiesChanged_callbacks.find(path + " @ " + conn.get_name());
                it->second.second(obj, it->second.first);
                PropertiesChanged_callbacks.erase(it);
            }
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["Get"].erase((*this)["Get"].find(type_id));
                (*this)["Set"].erase((*this)["Set"].find(type_id));
                (*this)["GetAll"].erase((*this)["GetAll"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_PROPERTIES_HPP