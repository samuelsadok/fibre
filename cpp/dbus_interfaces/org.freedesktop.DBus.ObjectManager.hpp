#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class org_freedesktop_DBus_ObjectManager : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "org.freedesktop.DBus.ObjectManager"; }

    org_freedesktop_DBus_ObjectManager(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_freedesktop_DBus_ObjectManager(const org_freedesktop_DBus_ObjectManager &) = delete;
    org_freedesktop_DBus_ObjectManager& operator=(const org_freedesktop_DBus_ObjectManager &) = delete;


    int GetManagedObjects_async(fibre::Callback<std::unordered_map<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>>* callback) {
        return method_call_async(get_interface_name(), "GetManagedObjects", callback);
    }

    fibre::DBusRemoteSignal<org_freedesktop_DBus_ObjectManager, DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>> InterfacesAdded{this, "InterfacesAdded"};
    fibre::DBusRemoteSignal<org_freedesktop_DBus_ObjectManager, DBusObject, std::vector<std::string>> InterfacesRemoved{this, "InterfacesRemoved"};

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "GetManagedObjects", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["GetManagedObjects"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<>, std::tuple<std::unordered_map<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>>>::template from_member_fn<TImpl, &TImpl::GetManagedObjects>((TImpl*)obj)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_OBJECTMANAGER_HPP