#ifndef __FIBRE_FUNC_UTILS_HPP
#define __FIBRE_FUNC_UTILS_HPP

#include <fibre/function.hpp>
#include <fibre/rich_status.hpp>
#include <fibre/../../codecs.hpp>
#include <fibre/fibre.hpp>
#include <cstddef>

namespace fibre {


template<typename T>
T decode(Domain* domain, cbufptr_t inbuf, RichStatus* status) {
    RichStatusOr<T> result = fibre::Codec<T>::decode(domain, &inbuf);
    if (result.has_value()) {
        return result.value();
    } else {
        *status = result.status();
        return T{};
    }
}

template<typename T>
bool encode(Domain* domain, T& val, bufptr_t buf, const uint8_t** end) {
    bool result = fibre::Codec<T>::encode(val, &buf);
    *end = buf.begin();
    return result;
}

/**
 * @brief Wraps a server-side function that completes synchronously such that it
 * is resumable can receive and send fragmented data.
 */
struct FuncAsCoro : Function {
    using TImpl = Callback<Status, Domain*, const uint8_t**, size_t, const uint8_t**, size_t*, bufptr_t>;

    FuncAsCoro(TImpl impl)
        : impl_(impl) {}

    Socket* start_call(
        Domain* domain, bufptr_t call_frame, Socket* caller) const final;

    FunctionInfo* get_info() const final { return nullptr; }
    void free_info(FunctionInfo* info) const final { }

    TImpl impl_;
};

struct ArgCollector {
    WriteResult write(WriteArgs args, bufptr_t storage);

    const uint8_t* arg_dividers_[8];
    size_t n_arg_dividers_;
    size_t offset_;
};

struct ArgEmitter {
    void start(Status status, const uint8_t** arg_dividers, size_t n_arg_dividers, Socket* sink);
    WriteArgs on_write_done(WriteResult result);

    Status status_ = kFibreOk;
    Chunk chunks_[8];
    BufChain tx_chain_;
};

struct FuncAsCoroCall : Socket {
    WriteResult write(WriteArgs args) final;
    WriteArgs on_write_done(WriteResult result) final;

    const FuncAsCoro* func;
    Domain* domain_;
    Socket* caller_;
    uint8_t* buf_end;
    std::variant<ArgCollector, ArgEmitter> collector_or_emitter_;
    bool invoked = false;
};

class CoroAsFunc : public Socket {
public:
    using TCallback = Callback<void, Socket*, Status, const cbufptr_t*, size_t>;

    CoroAsFunc(fibre::Function* func) : func(func) {}
    void call(const cbufptr_t* inputs, size_t n_inputs,
              TCallback on_call_finished);

private:
    WriteArgs on_write_done(WriteResult result);
    WriteResult write(WriteArgs args);

    fibre::Function* func;
    alignas(std::max_align_t) uint8_t call_frame[128];
    ArgEmitter emitter_;
    ArgCollector collector_;
    uint8_t tx_buf[128];
    uint8_t rx_buf[128];
    TCallback on_call_finished_;
};


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
    static Status sync_func_wrapper_impl(void* ptr, Domain* domain, const uint8_t** in_arg_dividers, size_t n_in_arg_dividers, const uint8_t** out_arg_dividers, size_t* n_out_arg_dividers, bufptr_t outbuf, std::index_sequence<Is...>, std::index_sequence<Js...>) {
        if (n_in_arg_dividers != sizeof...(Is) + 1) {
            *n_out_arg_dividers = 0;
            return kFibreProtocolError;
        }
        if (*n_out_arg_dividers < sizeof...(Js) + 1) {
            *n_out_arg_dividers = 0;
            return kFibreOutOfMemory;
        }

        RichStatus status;
        std::tuple<TIn...> inputs = {
            decode<TIn>(domain, cbufptr_t{in_arg_dividers[Is], in_arg_dividers[Is + 1]}, &status) ...
        };
        F_LOG_IF_ERR(domain->ctx->logger, status, "decoding failed");
        if (status.is_error()) {
            *n_out_arg_dividers = 0;
            return kFibreProtocolError; // TODO: decoders could fail for other reasons than insufficient data
        }
        auto func = (std::tuple<TOut...> (*)(TIn...))ptr;
        std::tuple<TOut...> outputs = std::apply(*func, inputs);
        out_arg_dividers[0] = outbuf.begin();
        std::array<bool, sizeof...(Js)> ok = {
            encode<TOut>(domain, std::get<Js>(outputs), bufptr_t{(uint8_t*)out_arg_dividers[Js], outbuf.end()}, &out_arg_dividers[Js + 1]) ...
        };
        if (std::any_of(ok.begin(), ok.end(), [](bool val) { return !val; })) {
            *n_out_arg_dividers = 0;
            return kFibreOutOfMemory;
        }
        *n_out_arg_dividers = sizeof...(Js) + 1;
        return kFibreClosed;
    };

    static Status sync_func_wrapper(void* ptr, Domain* domain, const uint8_t** in_arg_dividers, size_t n_in_arg_dividers, const uint8_t** out_arg_dividers, size_t* n_out_arg_dividers, bufptr_t outbuf) {
        return sync_func_wrapper_impl(ptr, domain, in_arg_dividers, n_in_arg_dividers, out_arg_dividers, n_out_arg_dividers, outbuf, std::make_index_sequence<sizeof...(TIn)>(), std::make_index_sequence<sizeof...(TOut)>());
    };

    template<typename T, T func, typename TObj = typename decltype(make_function_traits(func))::TObj>
    static FuncAsCoro::TImpl make_sync_member_func_wrapper() {
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

template<typename T, T func>
struct SyncMemberWrapper {
    static const FuncAsCoro instance;
};

template<typename T, T func>
const FuncAsCoro SyncMemberWrapper<T, func>::instance{
    Wrappers<typename decltype(make_function_traits(func))::TArgs, typename as_tuple<typename decltype(make_function_traits(func))::TRet>::type>::template make_sync_member_func_wrapper<T, func>()
};

}

#endif // __FIBRE_FUNC_UTILS_HPP
