#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_freedesktop_DBus_ObjectManager {
public:
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

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["GetManagedObjects"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::GetManagedObjects, (TImpl*)obj, (std::tuple<std::unordered_map<fibre::DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP