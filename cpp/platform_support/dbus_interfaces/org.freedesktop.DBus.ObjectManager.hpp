#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP

#include <fibre/platform_support/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_freedesktop_DBus_ObjectManager {
public:
    struct tag {};
    static const char* get_interface_name() { return "org.freedesktop.DBus.ObjectManager"; }

    org_freedesktop_DBus_ObjectManager(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_ObjectManager(const org_freedesktop_DBus_ObjectManager &) = delete;
    org_freedesktop_DBus_ObjectManager& operator=(const org_freedesktop_DBus_ObjectManager &) = delete;


    int GetManagedObjects_async(fibre::Callback<org_freedesktop_DBus_ObjectManager*, std::unordered_map<fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>>* callback) {
        return base_->method_call_async(this, "GetManagedObjects", callback);
    }

    fibre::DBusRemoteSignal<org_freedesktop_DBus_ObjectManager, fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>> InterfacesAdded{this, "InterfacesAdded"};
    fibre::DBusRemoteSignal<org_freedesktop_DBus_ObjectManager, fibre::DBusObjectPath, std::vector<std::string>> InterfacesRemoved{this, "InterfacesRemoved"};

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "GetManagedObjects", fibre::FunctionImplTable{} },
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;
        std::unordered_map<std::string, signal_table_entry_t<fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>> InterfacesAdded_callbacks{};
        std::unordered_map<std::string, signal_table_entry_t<fibre::DBusObjectPath, std::vector<std::string>>> InterfacesRemoved_callbacks{};

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["GetManagedObjects"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::GetManagedObjects, (TImpl*)obj, (std::tuple<std::unordered_map<fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>>*)nullptr)); }});
            }
            obj.InterfacesAdded += &(InterfacesAdded_callbacks.insert({path + " @ " + conn.get_name(), {fibre::make_closure(&fibre::DBusConnectionWrapper::emit_signal<org_freedesktop_DBus_ObjectManager, fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>, &conn).bind(std::string("InterfacesAdded")).bind(path), [](void* ctx, signal_closure_t<fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>& cb){ ((TImpl*)ctx)->InterfacesAdded -= &cb; } } }).first->second.first);
            obj.InterfacesRemoved += &(InterfacesRemoved_callbacks.insert({path + " @ " + conn.get_name(), {fibre::make_closure(&fibre::DBusConnectionWrapper::emit_signal<org_freedesktop_DBus_ObjectManager, fibre::DBusObjectPath, std::vector<std::string>>, &conn).bind(std::string("InterfacesRemoved")).bind(path), [](void* ctx, signal_closure_t<fibre::DBusObjectPath, std::vector<std::string>>& cb){ ((TImpl*)ctx)->InterfacesRemoved -= &cb; } } }).first->second.first);
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            {
                auto it = InterfacesAdded_callbacks.find(path + " @ " + conn.get_name());
                it->second.second(obj, it->second.first);
                InterfacesAdded_callbacks.erase(it);
            }
            {
                auto it = InterfacesRemoved_callbacks.find(path + " @ " + conn.get_name());
                it->second.second(obj, it->second.first);
                InterfacesRemoved_callbacks.erase(it);
            }
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["GetManagedObjects"].erase((*this)["GetManagedObjects"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP