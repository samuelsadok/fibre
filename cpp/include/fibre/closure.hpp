/**
 * @brief Provides alternative facilities to std::function with the benefit of
 * not allocating memory behind your back.
 * 
 * A Closure can store any kind of functor/function together with a list of
 * bound arguments. Bound arguments have the same purpose as the capture list of
 * a lambda function or the context pointer in classical C code.
 * 
 * The actual type of the Closure will depend on the type of the functor as well
 * as the captured arguments. Every closure implements the base type `Callable`
 * which specifies only on a function signature but not a "capture list".
 * 
 * This means that user code that takes as an argument a callback with a
 * specific signature can just a pointer to a Callable. In contrast to a
 * std::function based approach, the caller is in this case responsible for
 * keeping the Closure in memory for long enough.
 */

#ifndef __FIBRE_CLOSURE_HPP
#define __FIBRE_CLOSURE_HPP

#include <tuple>
#include "cpp_utils.hpp"

namespace fibre {

template<typename TOut, typename ... TIn>
struct Callable {
    virtual TOut operator()(TIn... in) const = 0;
};

template<typename ... TIn>
using Callback = Callable<void, TIn...>;

/**
 * @brief Replaces std::function which tends to allocate memory behind your back.
 * 
 * See description at the beginning of this file.
 */
template<typename TFunctor, typename TCapture, typename TIn, typename TOut>
struct Closure;

template<typename TBind, typename TClosure>
struct bind_result;

template<typename TFunctor, typename ... TCapture, typename ... TBind, typename ... TIn, typename TOut>
struct bind_result<std::tuple<TBind...>, Closure<TFunctor, std::tuple<TCapture...>, std::tuple<TIn...>, TOut>> {
    static_assert(std::is_same<tuple_take_t<sizeof...(TBind), std::tuple<TIn...>>, std::tuple<TBind...>>::value,
        "trying to bind the wrong types");
    using type = Closure<TFunctor, std::tuple<TCapture..., TBind...>, tuple_skip_t<sizeof...(TBind), std::tuple<TIn...>>, TOut>;
};

template<typename TClosure, typename ... TBind>
using bind_result_t = typename bind_result<std::tuple<TBind...>, TClosure>::type;

template<typename TFunctor, typename ... TCapture, typename ... TBind, typename ... TIn, typename TOut>
auto bind(Closure<TFunctor, std::tuple<TCapture...>, std::tuple<TBind..., TIn...>, TOut> fn, TBind&&... args) 
    -> Closure<TFunctor, std::tuple<TCapture..., TBind...>, std::tuple<TIn...>, TOut>
{
    return Closure<TFunctor, std::tuple<TCapture..., TBind...>, std::tuple<TIn...>, TOut>{
        fn.func_,
        std::tuple_cat(fn.ctx_, std::forward_as_tuple(args...))
    };
}


// Partial specialization for member functions with return type
template<typename TFunctor, typename ... TCapture, typename ... TIn, typename TOut>
struct Closure<TFunctor, std::tuple<TCapture...>, std::tuple<TIn...>, TOut> : public Callable<TOut, TIn...> {
    Closure(TFunctor func, std::tuple<TCapture...> ctx)
        : func_(func), ctx_(ctx) {}

    TOut operator()(TIn... in) const override {
        return std::apply(func_, std::tuple_cat(ctx_, std::forward_as_tuple(in...)));
    }

    template<typename ... TBind>
    bind_result_t<Closure, TBind...> bind(TBind... args) {
        return fibre::bind(*this, args...);
    }

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
    -> Closure<decltype(fn), std::tuple<TImpl*>, std::tuple<TIn...>, TOut>
{
    return Closure<decltype(fn), std::tuple<TImpl*>, std::tuple<TIn...>, TOut>{fn, obj};
}

template<typename F, typename TIn = tuple_skip_t<1, args_of_t<decltype(&F::operator())>>, typename TOut = result_of_t<decltype(&F::operator())>>
auto make_lambda_closure(F const& func)
    -> Closure<F, std::tuple<>, TIn, TOut>
{
    return Closure<F, std::tuple<>, TIn, TOut>{func, {}};
}

template<typename TImpl, typename ... TIn, typename TFnOut, typename ... TDesiredOut>
auto make_tuple_closure(TFnOut(TImpl::*fn)(TIn...), TImpl* obj, std::tuple<TDesiredOut...>*)
    -> Closure<FunctorReturningTuple<decltype(fn), std::tuple<TImpl*, TIn...>, std::tuple<TDesiredOut...>>, std::tuple<TImpl*>, std::tuple<TIn...>, std::tuple<TDesiredOut...>>
{
    // sorry for this line.
    return Closure<FunctorReturningTuple<decltype(fn), std::tuple<TImpl*, TIn...>, std::tuple<TDesiredOut...>>, std::tuple<TImpl*>, std::tuple<TIn...>, std::tuple<TDesiredOut...>>{FunctorReturningTuple<decltype(fn), std::tuple<TImpl*, TIn...>, std::tuple<TDesiredOut...>>{fn}, std::tuple<TImpl*>(obj)};
}

template<typename TSignature>
struct member_closure;

template<typename TOut, typename TImpl, typename ... TArgs>
struct member_closure<TOut (TImpl::*)(TArgs...)> {
    using type = Closure<TOut(TImpl::*)(TArgs...), std::tuple<TImpl*>, std::tuple<TArgs...>, TOut>;
};

template<typename TSignature>
using member_closure_t = typename member_closure<TSignature>::type;


}
#endif // __FIBRE_CLOSURE_HPP