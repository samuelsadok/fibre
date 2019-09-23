#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_freedesktop_DBus_Introspectable {
public:
    struct tag {};
    static const char* get_interface_name() { return "org.freedesktop.DBus.Introspectable"; }

    org_freedesktop_DBus_Introspectable(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_Introspectable(const org_freedesktop_DBus_Introspectable &) = delete;
    org_freedesktop_DBus_Introspectable& operator=(const org_freedesktop_DBus_Introspectable &) = delete;


    int Introspect_async(fibre::Callback<org_freedesktop_DBus_Introspectable*, std::string>* callback) {
        return base_->method_call_async(this, "Introspect", callback);
    }


    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "Introspect", fibre::FunctionImplTable{} },
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["Introspect"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Introspect, (TImpl*)obj, (std::tuple<std::string>*)nullptr)); }});
            }
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["Introspect"].erase((*this)["Introspect"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP