#ifndef __INTERFACES__IO_FIBRE_TESTINTERFACE_HPP
#define __INTERFACES__IO_FIBRE_TESTINTERFACE_HPP

#include <fibre/dbus.hpp>
#include <fibre/closure.hpp>
#include <vector>

class io_fibre_TestInterface {
public:
    struct tag {};
    static const char* get_interface_name() { return "io.fibre.TestInterface"; }

    io_fibre_TestInterface(fibre::DBusRemoteObjectBase* base)
        : base_(base) {}
    
    // For now we delete the copy constructor as we would need to change the references within the signal objects for copying an object properly
    io_fibre_TestInterface(const io_fibre_TestInterface &) = delete;
    io_fibre_TestInterface& operator=(const io_fibre_TestInterface &) = delete;


    int Func1_async(fibre::Callback<io_fibre_TestInterface*>* callback) {
        return base_->method_call_async(this, "Func1", callback);
    }

    int Func2_async(int32_t in_arg1, fibre::Callback<io_fibre_TestInterface*>* callback) {
        return base_->method_call_async(this, "Func2", callback, in_arg1);
    }

    int Func3_async(int32_t in_arg1, std::string in_arg2, fibre::Callback<io_fibre_TestInterface*>* callback) {
        return base_->method_call_async(this, "Func3", callback, in_arg1, in_arg2);
    }

    int Func4_async(fibre::Callback<io_fibre_TestInterface*, int32_t>* callback) {
        return base_->method_call_async(this, "Func4", callback);
    }

    int Func5_async(fibre::Callback<io_fibre_TestInterface*, int32_t, std::string>* callback) {
        return base_->method_call_async(this, "Func5", callback);
    }

    int Func6_async(int32_t in_arg1, std::string in_arg2, fibre::Callback<io_fibre_TestInterface*, std::string, uint32_t>* callback) {
        return base_->method_call_async(this, "Func6", callback, in_arg1, in_arg2);
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
        std::unordered_map<fibre::dbus_type_id_t, size_t> ref_count{}; // keeps track of how often a given type has been registered

        template<typename ... TArgs>
        using signal_closure_t = fibre::Closure<void(fibre::DBusConnectionWrapper::*)(std::string, fibre::DBusObjectPath, TArgs...), std::tuple<fibre::DBusConnectionWrapper*, std::string, fibre::DBusObjectPath>, std::tuple<TArgs...>, void>;

        template<typename ... TArgs>
        using signal_table_entry_t = std::pair<signal_closure_t<TArgs...>, void(*)(void*, signal_closure_t<TArgs...>&)>;
        std::unordered_map<std::string, signal_table_entry_t<>> Signal1_callbacks{};
        std::unordered_map<std::string, signal_table_entry_t<int32_t>> Signal2_callbacks{};
        std::unordered_map<std::string, signal_table_entry_t<int32_t, std::string>> Signal3_callbacks{};

        template<typename TImpl>
        void register_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, TImpl& obj) {
            if (ref_count[fibre::get_type_id<TImpl>()]++ == 0) {
                (*this)["Func1"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Func1, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["Func2"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Func2, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["Func3"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Func3, (TImpl*)obj, (std::tuple<>*)nullptr)); }});
                (*this)["Func4"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Func4, (TImpl*)obj, (std::tuple<int32_t>*)nullptr)); }});
                (*this)["Func5"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Func5, (TImpl*)obj, (std::tuple<int32_t, std::string>*)nullptr)); }});
                (*this)["Func6"].insert({fibre::get_type_id<TImpl>(), [](void* obj, DBusMessage* rx_msg, DBusMessage* tx_msg){ return fibre::DBusConnectionWrapper::handle_method_call_typed(rx_msg, tx_msg, fibre::make_tuple_closure(&TImpl::Func6, (TImpl*)obj, (std::tuple<std::string, uint32_t>*)nullptr)); }});
            }
            obj.Signal1 += &(Signal1_callbacks.insert({path + " @ " + conn.get_name(), {fibre::make_closure(&fibre::DBusConnectionWrapper::emit_signal<io_fibre_TestInterface>, &conn).bind(std::string("Signal1")).bind(path), [](void* ctx, signal_closure_t<>& cb){ ((TImpl*)ctx)->Signal1 -= &cb; } } }).first->second.first);
            obj.Signal2 += &(Signal2_callbacks.insert({path + " @ " + conn.get_name(), {fibre::make_closure(&fibre::DBusConnectionWrapper::emit_signal<io_fibre_TestInterface, int32_t>, &conn).bind(std::string("Signal2")).bind(path), [](void* ctx, signal_closure_t<int32_t>& cb){ ((TImpl*)ctx)->Signal2 -= &cb; } } }).first->second.first);
            obj.Signal3 += &(Signal3_callbacks.insert({path + " @ " + conn.get_name(), {fibre::make_closure(&fibre::DBusConnectionWrapper::emit_signal<io_fibre_TestInterface, int32_t, std::string>, &conn).bind(std::string("Signal3")).bind(path), [](void* ctx, signal_closure_t<int32_t, std::string>& cb){ ((TImpl*)ctx)->Signal3 -= &cb; } } }).first->second.first);
        }

        int deregister_implementation(fibre::DBusConnectionWrapper& conn, fibre::DBusObjectPath path, void* obj, fibre::dbus_type_id_t type_id) {
            {
                auto it = Signal1_callbacks.find(path + " @ " + conn.get_name());
                it->second.second(obj, it->second.first);
                Signal1_callbacks.erase(it);
            }
            {
                auto it = Signal2_callbacks.find(path + " @ " + conn.get_name());
                it->second.second(obj, it->second.first);
                Signal2_callbacks.erase(it);
            }
            {
                auto it = Signal3_callbacks.find(path + " @ " + conn.get_name());
                it->second.second(obj, it->second.first);
                Signal3_callbacks.erase(it);
            }
            auto it = ref_count.find(type_id);
            if (it == ref_count.end()) {
                return -1;
            }
            if (--(it->second) == 0) {
                (*this)["Func1"].erase((*this)["Func1"].find(type_id));
                (*this)["Func2"].erase((*this)["Func2"].find(type_id));
                (*this)["Func3"].erase((*this)["Func3"].find(type_id));
                (*this)["Func4"].erase((*this)["Func4"].find(type_id));
                (*this)["Func5"].erase((*this)["Func5"].find(type_id));
                (*this)["Func6"].erase((*this)["Func6"].find(type_id));
                ref_count.erase(it);
            }
            return 0;
        }
    };

    fibre::DBusRemoteObjectBase* base_;
};

#endif // __INTERFACES__IO_FIBRE_TESTINTERFACE_HPP