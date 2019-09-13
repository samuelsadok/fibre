#ifndef __INTERFACES__ORG_BLUEZ_GATTSERVICE1_HPP
#define __INTERFACES__ORG_BLUEZ_GATTSERVICE1_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_bluez_GattService1 {
public:
    static const char* get_interface_name() { return "org.bluez.GattService1"; }

    org_bluez_GattService1(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    org_bluez_GattService1(const org_bluez_GattService1 &) = delete;
    org_bluez_GattService1& operator=(const org_bluez_GattService1 &) = delete;


    // DBusProperty<std::string> UUID;
    // DBusProperty<fibre::DBusObjectPath> Device;
    // DBusProperty<bool> Primary;
    // DBusProperty<std::vector<fibre::DBusObjectPath>> Characteristics;

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
        } {}
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
            }
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_BLUEZ_GATTSERVICE1_HPP