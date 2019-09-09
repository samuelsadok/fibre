#ifndef __FIBRE_CALLBACK_HPP
#define __FIBRE_CALLBACK_HPP

#include <tuple>
#include "cpp_utils.hpp"

namespace fibre {

template<typename ... T>
struct Callback {
    void (*callback)(void*, T...);
    void* ctx;
};


/**
 * @brief Replaces std::function which tends to allocate memory behind your back.
 */
template<typename TCapture, typename TIn, typename TOut>
struct GenericFunction;

template<typename ... TCapture, typename ... TIn>
struct GenericFunction<std::tuple<TCapture...>, std::tuple<TIn...>, void> {
    //using type = decltype([std::declval<TCapture>()...] (TIn...) { return TOut })
    void (*callback)(TCapture..., TIn...);
    std::tuple<TCapture...> ctx;

    void operator()(TIn... in) {
        std::apply(callback, std::tuple_cat(ctx, std::make_tuple(in...)));
    }
};

struct dummy;

template<typename T>
struct single_tuple_type { using type = dummy; };

template<typename T>
struct single_tuple_type<std::tuple<T>> { using type = T; };

template<typename ... TCapture, typename ... TIn, typename TOut>
struct GenericFunction<std::tuple<TCapture...>, std::tuple<TIn...>, TOut> {
    //using type = decltype([std::declval<TCapture>()...] (TIn...) { return TOut })
    TOut (*callback)(TCapture..., TIn...);
    std::tuple<TCapture...> ctx;

    template<typename TImpl, TOut(TImpl::*Func)(TIn...)>
    static GenericFunction from_member_fn(TImpl* obj) {
        return GenericFunction{
            [](TImpl* obj, TIn... in) {
                return (obj->*Func)(in...);
            },
            std::tuple<TImpl*>{obj}
        };
    }

    template<typename TImpl, typename single_tuple_type<TOut>::type (TImpl::*Func)(TIn...)>
    static GenericFunction from_member_fn(TImpl* obj) {
        return GenericFunction{
            [](TImpl* obj, TIn... in) {
                return TOut{(obj->*Func)(in...)};
            },
            std::tuple<TImpl*>{obj}
        };
    }

    template<typename TImpl, void(TImpl::*Func)(TIn...)>
    static GenericFunction from_member_fn(TImpl* obj) {
        return GenericFunction{
            [](TImpl* obj, TIn... in) {
                (obj->*Func)(in...);
                return std::tuple<>{};
            },
            std::tuple<TImpl*>{obj}
        };
    }

    TOut operator()(TIn... in) {
        return std::apply(callback, std::tuple_cat(ctx, std::make_tuple(in...)));
    }
};



template<typename ... TCapture, typename ... TBind, typename ... TIn, typename ... TOut>
auto bind_arg(GenericFunction<std::tuple<TCapture...>, std::tuple<TBind..., TIn...>, std::tuple<TOut...>> fn, TBind... args) 
    -> GenericFunction<std::tuple<TCapture..., TBind...>, std::tuple<TIn...>, std::tuple<TOut...>>
{
    return {
        fn.callback,
        std::tuple_cat(fn.ctx, std::make_tuple(args...))
    };
}



}
#endif // __FIBRE_CALLBACK_HPP