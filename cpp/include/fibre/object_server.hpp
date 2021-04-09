#ifndef __FIBRE_OBJECT_SERVER_HPP
#define __FIBRE_OBJECT_SERVER_HPP

#include "fibre.hpp"
#include "../../static_exports.hpp"
#include "../../codecs.hpp"
#include <algorithm>

namespace fibre {

template<typename T>
constexpr ServerObjectDefinition make_obj(T* obj) {
    return {obj, get_interface_id<T>()};
}

template<typename T>
constexpr ServerObjectDefinition make_obj(const T* obj) {
    return {const_cast<T*>(obj), get_interface_id<const T>()};
}

template<typename T>
T decode(Domain* domain, cbufptr_t* inbuf, RichStatus* status) {
    RichStatusOr<T> result = fibre::Codec<T>::decode(domain, inbuf);
    if (result.has_value()) {
        return result.value();
    } else {
        *status = result.status();
        return T{};
    }
}

struct SyncWrapper {
    Callback<Status, Domain*, cbufptr_t, bufptr_t*> sync_entrypoint;

    // The call context is guaranteed to always have the same size for an
    // ongoing call
    // TODO: what about alignment guarantees?
    std::optional<CallBufferRelease> entrypoint(Domain* domain, bool start, bufptr_t call_context, CallBuffers call_buffers, Callback<std::optional<CallBuffers>, CallBufferRelease> continuation);
};

std::optional<CallBufferRelease> SyncWrapper::entrypoint(Domain* domain, bool start, bufptr_t call_context, CallBuffers call_buffers, Callback<std::optional<CallBuffers>, CallBufferRelease> continuation) {
    struct CallState {
        bool tx_phase = false;
        size_t offset = 0;
        size_t tx_len = 0; // only valid in TX phase
    };

    if (call_context.size() < sizeof(CallState)) {
        return CallBufferRelease{kFibreOutOfMemory, call_buffers.tx_buf.begin(), call_buffers.rx_buf.begin()};
    }

    CallState& state = *(CallState*)call_context.begin();
    bufptr_t arg_memory = call_context.skip(sizeof(CallState));

    if (start) {
        state = CallState{}; // initialize call state
    }

    if (!state.tx_phase) {
        // Copy caller's input buffer into call's state buffer
        size_t n_copy = std::min(call_buffers.tx_buf.size(), arg_memory.skip(state.offset).size());
        std::copy_n(call_buffers.tx_buf.begin(), n_copy, arg_memory.skip(state.offset).begin());
        state.offset += n_copy;
        call_buffers.tx_buf.skip(n_copy);

        bufptr_t outbuf = arg_memory;
        Status call_status = sync_entrypoint.invoke(domain, arg_memory.take(state.offset), &outbuf);
        if (call_status != kFibreClosed && call_status != kFibreInsufficientData) {
            // Synchronous call failed
            return CallBufferRelease{call_status, call_buffers.tx_buf.begin(), call_buffers.rx_buf.begin()};
        }
        if (call_status == kFibreInsufficientData && state.offset == arg_memory.size()) {
            // Context buffer too small to hold the input args for this function
            return CallBufferRelease{kFibreOutOfMemory, call_buffers.tx_buf.begin(), call_buffers.rx_buf.begin()};
        }
        if (call_status != kFibreInsufficientData) {
            // Synchronous call succeeded - change over to TX phase
            state.tx_len = outbuf.begin() - arg_memory.begin();
            state.tx_phase = true;
            state.offset = 0;
        }
    }

    if (state.tx_phase) {
        // Copy call's state buffer into caller's output buffer
        size_t n_copy = std::min(call_buffers.rx_buf.size(), state.tx_len - state.offset);
        std::copy_n(arg_memory.skip(state.offset).begin(), n_copy, call_buffers.rx_buf.begin());
        state.offset += n_copy;
        call_buffers.rx_buf.skip(n_copy);
    }

    Status status = state.offset == state.tx_len ? kFibreClosed : kFibreOk;
    return CallBufferRelease{status, call_buffers.tx_buf.begin(), call_buffers.rx_buf.begin()};
}


template<typename TIn, typename TOut>
struct Wrappers;

template<typename ... TIn, typename ... TOut>
struct Wrappers<std::tuple<TIn...>, std::tuple<TOut...>> {
    /**
     * Note: this template function should be kept simple as it's instantiated for
     * every call signature. Functions of the same call signature share the same
     * instantiation of this template.
     * 
     * @param ptr: A function that takes sizeof...(TIn) input arguments and
     *             returns a tuple with sizeof...(TOut) output arguments.
     */
    template<size_t ... Is, size_t ... Js>
    static Status sync_func_wrapper_impl(void* ptr, Domain* domain, cbufptr_t inbuf, bufptr_t* outbuf, std::index_sequence<Is...>, std::index_sequence<Js...>) {
        RichStatus status;
        std::tuple<TIn...> inputs = {
            decode<TIn>(domain, &inbuf, &status) ...
        };
        F_LOG_IF_ERR(domain->ctx->logger, status, "decoding failed");
        if (status.is_error()) {
            return kFibreInsufficientData; // TODO: decoders could fail for other reasons than insufficient data
        }
        auto func = (std::tuple<TOut...> (*)(TIn...))ptr;
        std::tuple<TOut...> outputs = std::apply(*func, inputs);
        std::array<bool, sizeof...(Js)> ok = {fibre::Codec<uint32_t>::encode(std::get<Js>(outputs), outbuf) ...};
        return std::all_of(ok.begin(), ok.end(), [](bool val) { return val; }) ? kFibreClosed : kFibreOutOfMemory;
    };

    static Status sync_func_wrapper(void* ptr, Domain* domain, cbufptr_t inbuf, bufptr_t* outbuf) {
        return sync_func_wrapper_impl(ptr, domain, inbuf, outbuf, std::make_index_sequence<sizeof...(TIn)>(), std::make_index_sequence<sizeof...(TOut)>());
    };

    template<typename T, T func, typename TObj = typename decltype(make_function_traits(func))::TObj>
    static Callback<Status, Domain*, cbufptr_t, bufptr_t*> make_sync_member_func_wrapper() {
        std::tuple<TOut...> (*free_func)(TObj*, TIn...) = [](TObj* obj, TIn ... args) {
            return as_tuple<typename result_of<T>::type>::wrap_result(
                [obj, &args...]() { return (obj->*func)(args...); }
            );
        };
        return {
            Wrappers<std::tuple<TObj*, TIn...>, std::tuple<TOut...>>::sync_func_wrapper,
            (void*)free_func
        };
    }
};

template<typename T, T func> SyncWrapper sync_member_func_wrapper = {
    .sync_entrypoint = Wrappers<typename decltype(make_function_traits(func))::TArgs, typename as_tuple<typename decltype(make_function_traits(func))::TRet>::type>::template make_sync_member_func_wrapper<T, func>()
};

}

#endif // __FIBRE_OBJECT_SERVER_HPP
