#ifndef __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP
#define __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class org_freedesktop_DBus_Introspectable {
public:
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

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["Introspect"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Introspect, (TImpl*)obj, (std::tuple<std::string>*)nullptr)); }});
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__ORG_FREEDESKTOP_DBUS_INTROSPECTABLE_HPP