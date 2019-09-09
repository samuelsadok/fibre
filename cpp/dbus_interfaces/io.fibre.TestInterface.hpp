#ifndef __INTERFACES__IO_FIBRE_TESTINTERFACE_HPP
#define __INTERFACES__IO_FIBRE_TESTINTERFACE_HPP

#include <fibre/dbus.hpp>
#include <fibre/callback.hpp>
#include <vector>

class io_fibre_TestInterface : public fibre::DBusObject {
public:
    static const char* get_interface_name() { return "io.fibre.TestInterface"; }

    io_fibre_TestInterface(fibre::DBusConnectionWrapper* conn, const char* service_name, const char* object_name)
        : DBusObject(conn, service_name, object_name) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    io_fibre_TestInterface(const io_fibre_TestInterface &) = delete;
    io_fibre_TestInterface& operator=(const io_fibre_TestInterface &) = delete;


    int Func1_async(fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "Func1", callback);
    }

    int Func2_async(int32_t in_arg1, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "Func2", callback, in_arg1);
    }

    int Func3_async(int32_t in_arg1, std::string in_arg2, fibre::Callback<>* callback) {
        return method_call_async(get_interface_name(), "Func3", callback, in_arg1, in_arg2);
    }

    int Func4_async(fibre::Callback<int32_t>* callback) {
        return method_call_async(get_interface_name(), "Func4", callback);
    }

    int Func5_async(fibre::Callback<int32_t, std::string>* callback) {
        return method_call_async(get_interface_name(), "Func5", callback);
    }

    int Func6_async(int32_t in_arg1, std::string in_arg2, fibre::Callback<std::string, uint32_t>* callback) {
        return method_call_async(get_interface_name(), "Func6", callback, in_arg1, in_arg2);
    }

    fibre::DBusRemoteSignal<io_fibre_TestInterface> Signal1{this, "Signal1"};
    fibre::DBusRemoteSignal<io_fibre_TestInterface, int32_t> Signal2{this, "Signal2"};
    fibre::DBusRemoteSignal<io_fibre_TestInterface, int32_t, std::string> Signal3{this, "Signal3"};

    struct ExportTable : fibre::ExportTableBase {
        ExportTable() : fibre::ExportTableBase{
            { "Func1", fibre::FunctionImplTable{} },
            { "Func2", fibre::FunctionImplTable{} },
            { "Func3", fibre::FunctionImplTable{} },
            { "Func4", fibre::FunctionImplTable{} },
            { "Func5", fibre::FunctionImplTable{} },
            { "Func6", fibre::FunctionImplTable{} },
        } {}

        template<typename TImpl>
        int register_implementation(TImpl& obj) {
            (*this)["Func1"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::Func1>((TImpl*)obj)); }});
            (*this)["Func2"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<int32_t>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::Func2>((TImpl*)obj)); }});
            (*this)["Func3"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<int32_t, std::string>, std::tuple<>>::template from_member_fn<TImpl, &TImpl::Func3>((TImpl*)obj)); }});
            (*this)["Func4"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<>, std::tuple<int32_t>>::template from_member_fn<TImpl, &TImpl::Func4>((TImpl*)obj)); }});
            (*this)["Func5"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<>, std::tuple<int32_t, std::string>>::template from_member_fn<TImpl, &TImpl::Func5>((TImpl*)obj)); }});
            (*this)["Func6"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::GenericFunction<std::tuple<TImpl*>, std::tuple<int32_t, std::string>, std::tuple<std::string, uint32_t>>::template from_member_fn<TImpl, &TImpl::Func6>((TImpl*)obj)); }});
            return 0;
        }
    };
};

#endif // __INTERFACES__IO_FIBRE_TESTINTERFACE_HPP