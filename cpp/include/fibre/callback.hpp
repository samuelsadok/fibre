#ifndef __FIBRE_CALLBACK_HPP
#define __FIBRE_CALLBACK_HPP

#include <tuple>
#include "cpp_utils.hpp"

namespace fibre {

/**
 * @brief Replaces std::function which tends to allocate memory behind your back.
 * 
 * TODO: FunctorClosure is a better generalization of this.
 */
template<typename TObj, typename TCapture, typename TIn, typename TOut>
struct Closure;

template<typename TFunctor, typename TCapture, typename TIn, typename TOut>
struct FunctorClosure;

template<typename TBind, typename TClosure>
struct bind_result;

template<typename TObj, typename ... TCapture, typename ... TBind, typename ... TIn, typename TOut>
struct bind_result<std::tuple<TBind...>, Closure<TObj, std::tuple<TCapture...>, std::tuple<TBind..., TIn...>, TOut>> {
    using type = Closure<TObj, std::tuple<TCapture..., TBind...>, std::tuple<TIn...>, TOut>;
};

template<typename TClosure, typename ... TBind>
using bind_result_t = typename bind_result<std::tuple<TBind...>, TClosure>::type;

template<typename TObj, typename ... TCapture, typename ... TBind, typename ... TIn, typename TOut>
auto bind(Closure<TObj, std::tuple<TCapture...>, std::tuple<TBind..., TIn...>, TOut> fn, TBind&&... args) 
    -> Closure<TObj, std::tuple<TCapture..., TBind...>, std::tuple<TIn...>, TOut>
{
    return Closure<TObj, std::tuple<TCapture..., TBind...>, std::tuple<TIn...>, TOut>{
        fn.callback_,
        std::tuple_cat(fn.ctx_, std::forward_as_tuple(args...))
    };
}


template<typename TOut, typename ... TIn>
struct GenericFunction {
    virtual TOut operator()(TIn... in) const = 0;
};

template<typename ... TIn>
using Callback = GenericFunction<void, TIn...>;


// Partial specialization for non-member functions with return type
template<typename ... TCapture, typename ... TIn, typename TOut>
struct Closure<void, std::tuple<TCapture...>, std::tuple<TIn...>, TOut> : public GenericFunction<TOut, TIn...> {
    Closure(TOut(*callback)(TCapture..., TIn...), std::tuple<TCapture...> ctx)
        : callback_(callback), ctx_(ctx) {}

    TOut operator()(TIn... in) const override {
        return std::apply(callback_, std::tuple_cat(ctx_, std::forward_as_tuple(in...)));
        //return std::apply(callback_, std::tuple_cat(ctx_, std::make_tuple(in...)));
    }

    template<typename ... TBind>
    bind_result_t<Closure, TBind...> bind(TBind... args) {
        return fibre::bind(*this, args...);
    }

    TOut(*callback_)(TCapture..., TIn...);
    std::tuple<TCapture...> ctx_;
};

// Partial specialization for member functions with return type
template<typename TImpl, typename ... TCapture, typename ... TIn, typename TOut>
struct Closure<TImpl, std::tuple<TImpl*, TCapture...>, std::tuple<TIn...>, TOut> : public GenericFunction<TOut, TIn...> {
    Closure(TOut(TImpl::*callback)(TCapture..., TIn...), std::tuple<TImpl*, TCapture...> ctx)
        : callback_(callback), ctx_(ctx) {}

    TOut operator()(TIn... in) const override {
        return std::apply(callback_, std::tuple_cat(ctx_, std::forward_as_tuple(in...)));
    }

    template<typename ... TBind>
    bind_result_t<Closure, TBind...> bind(TBind... args) {
        return fibre::bind(*this, args...);
    }

    TOut(TImpl::*callback_)(TCapture..., TIn...);
    std::tuple<TImpl*, TCapture...> ctx_;
};

// Partial specialization for member functions with return type
template<typename TFunctor, typename ... TCapture, typename ... TIn, typename TOut>
struct FunctorClosure<TFunctor, std::tuple<TCapture...>, std::tuple<TIn...>, TOut> : public GenericFunction<TOut, TIn...> {
    FunctorClosure(TFunctor func, std::tuple<TCapture...> ctx)
        : func_(func), ctx_(ctx) {}

    TOut operator()(TIn... in) const override {
        return std::apply(func_, std::tuple_cat(ctx_, std::forward_as_tuple(in...)));
    }

    /*template<typename ... TBind>
    bind_result_t<FunctorClosure, TBind...> bind(TBind... args) {
        return fibre::bind(*this, args...);
    }*/

    TFunctor func_;
    std::tuple<TCapture...> ctx_;
};


template<typename TFunctor, typename TIn, typename TOut>
struct FunctorReturningTuple;

template<typename TFunctor, typename ... TIn>
struct FunctorReturningTuple<TFunctor, std::tuple<TIn...>, std::tuple<>> {
    std::tuple<> operator()(TIn... in) const {
        std::apply(func_, std::forward_as_tuple(in...));
        return std::tuple<>{};
    }
    TFunctor func_;
};

template<typename TFunctor, typename ... TIn, typename ... TOut>
struct FunctorReturningTuple<TFunctor, std::tuple<TIn...>, std::tuple<TOut...>> {
    std::tuple<TOut...> operator()(TIn... in) const {
        return std::apply(func_, std::forward_as_tuple(in...));
    }
    TFunctor func_;
};


template<typename ... TIn, typename TOut>
auto make_closure(TOut(*fn)(TIn...))
    -> Closure<void, std::tuple<>, std::tuple<TIn...>, TOut>
{
    return Closure<void, std::tuple<>, std::tuple<TIn...>, TOut>{fn};
}

template<typename TImpl, typename ... TIn, typename TOut>
auto make_closure(TOut(TImpl::*fn)(TIn...), TImpl* obj)
    -> Closure<TImpl, std::tuple<TImpl*>, std::tuple<TIn...>, TOut>
{
    return Closure<TImpl, std::tuple<TImpl*>, std::tuple<TIn...>, TOut>{fn, obj};
}

template<typename F>
auto make_lambda_closure(F const& func)
    -> Closure<void, std::tuple<>, tuple_skip_t<1, args_of_t<decltype(&F::operator())>>, result_of_t<decltype(&F::operator())>>
{
    return Closure<void, std::tuple<>, tuple_skip_t<1, args_of_t<decltype(&F::operator())>>, result_of_t<decltype(&F::operator())>>{func, {}};
}

template<typename TImpl, typename ... TIn, typename TFnOut, typename ... TDesiredOut>
auto make_tuple_closure(TFnOut(TImpl::*fn)(TIn...), TImpl* obj, std::tuple<TDesiredOut...>*)
    -> FunctorClosure<FunctorReturningTuple<decltype(fn), std::tuple<TImpl*, TIn...>, std::tuple<TDesiredOut...>>, std::tuple<TImpl*>, std::tuple<TIn...>, std::tuple<TDesiredOut...>>
{
    // sorry for this line.
    return FunctorClosure<FunctorReturningTuple<decltype(fn), std::tuple<TImpl*, TIn...>, std::tuple<TDesiredOut...>>, std::tuple<TImpl*>, std::tuple<TIn...>, std::tuple<TDesiredOut...>>{FunctorReturningTuple<decltype(fn), std::tuple<TImpl*, TIn...>, std::tuple<TDesiredOut...>>{fn}, std::tuple<TImpl*>(obj)};
}


}
#endif // __FIBRE_CALLBACK_HPP