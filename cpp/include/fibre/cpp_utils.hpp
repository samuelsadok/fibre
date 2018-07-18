/*

## Advanced C++ Topics

This is an overview of some of the more obscure C++ techniques used in this project.
This assumes you're already familiar with templates in C++.

### Template recursion

[TODO]

### Almost perfect template forwarding

This is adapted from https://akrzemi1.wordpress.com/2013/10/10/too-perfect-forwarding/

Suppose you have a inner class, with a couple of constructors:
```
class InnerClass {
public:
    InnerClass(int arg1, int arg2);
    InnerClass(int arg1);
    InnerClass();
};
```

Now you want to create a wrapper class. This wrapper class should provide the exact same constructors as `InnerClass`, so you use perfect forwarding:
```
class WrapperClass {
public:
    template<typename ... Args>
    WrapperClass(Args&& ... args)
        : inner_object(std::forward<Args>(args)...)
    {}
    InnerClass inner_object;
};
```

Now you can almost use the wrapper class as expected, but only almost:
```
void make_wrappers(void) {
    WrapperClass wrapper1;              // ok, maps to InnerClass()
    WrapperClass wrapper2(1);           // ok, maps to InnerClass(int arg1)
    WrapperClass wrapper3(1,2);         // ok, maps to InnerClass(int arg1, arg2)
    WrapperClass wrapper4 = wrapper1;   // does not compile
}
```

The last assignment fails. What _you_ obviously wanted, is to use the copy constructor of WrapperClass.
However the compiler will use the perfect forwarding constructor of WrapperClass for this assignment.
So after template expansion it would try to use the following constructor:

```
    WrapperClass(InnerClass& arg)
        : inner_object(arg)
    {}
```

Clearly this is not what we wanted and in this case it will fail because the exists no
constructor of the form `InnerClass(WrapperClass& arg)`.

And thus we need to make the perfect forwarding a little less perfect, by telling it
"only enable this constructor if the first argument of the argument list is not of type WrapperClass".

The modified version thus looks like this:
```
class WrapperClass {
public:
    template<typename ... Args, ENABLE_IF(TypeChecker<Args...>::template first_is_not<WrapperClass>())>
    WrapperClass(Args&& ... args)
        : inner_object(std::forward<Args>(args)...)
    {} 
    InnerClass inner_object;
};
```

*/

#ifndef __CPP_UTILS_HPP
#define __CPP_UTILS_HPP

#include <limits>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include <iomanip>

/* Backport features from C++14 and C++17 ------------------------------------*/

#if __cplusplus < 201402L
namespace std {
    template< class T >
    using underlying_type_t = typename underlying_type<T>::type;

    // source: http://en.cppreference.com/w/cpp/types/enable_if
    template< bool B, class T = void >
    using enable_if_t = typename enable_if<B,T>::type;

    // source: http://en.cppreference.com/w/cpp/utility/tuple/tuple_element
    template <std::size_t I, class T>
    using tuple_element_t = typename tuple_element<I, T>::type;

    // source: https://en.cppreference.com/w/cpp/types/remove_cv
    template< class T >
    using remove_cv_t       = typename remove_cv<T>::type;
    template< class T >
    using remove_const_t    = typename remove_const<T>::type;
    template< class T >
    using remove_volatile_t = typename remove_volatile<T>::type;

    template< class T >
    using decay_t = typename decay<T>::type;

    /// Class template integer_sequence
    template<typename _Tp, _Tp... _Idx>
    struct integer_sequence {
        typedef _Tp value_type;
        static constexpr size_t size() noexcept { return sizeof...(_Idx); }
    };

    /// Alias template make_integer_sequence
    // TODO: __integer_pack is a GCC built-in => use template metaprogramming
    template<typename _Tp, _Tp _Num>
    using make_integer_sequence = integer_sequence<_Tp, __integer_pack(_Num)...>;

    /// Alias template index_sequence
    template<size_t... _Idx>
    using index_sequence = integer_sequence<size_t, _Idx...>;

    /// Alias template make_index_sequence
    template<size_t _Num>
    using make_index_sequence = make_integer_sequence<size_t, _Num>;
}
#endif

namespace fibre {
    template<typename _Tp, _Tp IFrom, _Tp ITo, _Tp ... I>
    struct make_integer_sequence_from_to
        : public make_integer_sequence_from_to<_Tp, IFrom, ITo - 1, I...> {};

    template<typename _Tp, _Tp IFrom, _Tp ... I>
    struct make_integer_sequence_from_to<_Tp, IFrom, IFrom, I...>
        : public std::index_sequence<I...> {};
}

#if __cplusplus < 201703L
namespace std {
//template<typename Fn, typename... Args,
//    std::enable_if_t<std::is_member_pointer<std::decay_t<Fn>>{}, int> = 0>
//using enable_

template <class, class, class...> struct invoke_result_impl;

template<typename Fn, typename... Args>
struct invoke_result_impl<std::enable_if_t<std::is_member_pointer<std::decay_t<Fn>>{}>,
                          Fn, Args...> {
    typedef decltype(std::mem_fn(std::declval<Fn>())(std::declval<Args>()...)) type;
};

template<typename Fn, typename... Args>
struct invoke_result_impl<std::enable_if_t<!std::is_member_pointer<std::decay_t<Fn>>{}>,
                          Fn, Args...> {
    typedef decltype(std::declval<Fn>()(std::declval<Args>()...)) type;
};

template<typename Fn, typename... Args>
using invoke_result = invoke_result_impl<void, Fn, Args...>;

template<typename Fn, typename... Args>
using invoke_result_t = typename invoke_result<Fn, Args...>::type;

template<typename Fn, typename... Args,
        std::enable_if_t<std::is_member_pointer<std::decay_t<Fn>>{}, int> = 0 >
constexpr invoke_result_t<Fn, Args...> invoke(Fn&& f, Args&&... args)
    noexcept(noexcept(std::mem_fn(f)(std::forward<Args>(args)...)))
{
    return std::mem_fn(f)(std::forward<Args>(args)...);
}

template<typename Fn, typename... Args, 
         std::enable_if_t<!std::is_member_pointer<std::decay_t<Fn>>{}, int> = 0>
constexpr invoke_result_t<Fn, Args...> invoke(Fn&& f, Args&&... args)
    noexcept(noexcept(std::forward<Fn>(f)(std::forward<Args>(args)...)))
{
    return std::forward<Fn>(f)(std::forward<Args>(args)...);
}
}

namespace std {
namespace detail {
template <class F, class Tuple, class>
struct apply_result_impl;

// TODO: apply_result is not part of C++17, therefore we should move this out of
// the #if block
template <class F, class Tuple, std::size_t... I>
struct apply_result_impl<F, Tuple, std::index_sequence<I...>> {
    //typedef std::invoke_result_t<F, std::tuple_element_t<I, Tuple>...> type;
    typedef std::invoke_result_t<F, decltype(std::get<I>(std::declval<Tuple>()))...> type;
};

template <class F, class Tuple>
using apply_result = apply_result_impl<F, Tuple, std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>>;

template <class F, class Tuple>
using apply_result_t = typename apply_result<F, Tuple>::type;

template <class F, class Tuple, std::size_t... I>
constexpr apply_result_t<F, Tuple> apply_impl( F&& f, Tuple&& t, std::index_sequence<I...> )
{
    return std::invoke(std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);
}
} // namespace detail

template <class F, class Tuple>
constexpr detail::apply_result_t<F, Tuple> apply(F&& f, Tuple&& t)
{
    return detail::apply_impl(std::forward<F>(f), std::forward<Tuple>(t),
        std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{});
}
}

#endif

/* Stuff that should be in the STL but isn't ---------------------------------*/

// source: https://en.cppreference.com/w/cpp/experimental/to_array
namespace detail {
template <class T, std::size_t N, std::size_t... I>
constexpr std::array<std::remove_cv_t<T>, N>
    to_array_impl(T (&a)[N], std::index_sequence<I...>)
{
    return { {a[I]...} };
}

template <class T, std::size_t N>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N])
{
    return detail::to_array_impl(a, std::make_index_sequence<N>{});
}
}



/* Custom utils --------------------------------------------------------------*/

// @brief Supports various queries on a list of types
template<typename ... Ts>
class TypeChecker;

template<typename T, typename ... Ts>
class TypeChecker<T, Ts...> {
public:
    using DecayedT = typename std::decay<T>::type;
    
    // @brief Returns false if type T is equal to U or inherits from U. Returns true otherwise.
    template<typename U>
    constexpr static inline bool first_is_not() {
        return !std::is_same<DecayedT, U>::value
            && !std::is_base_of<U, DecayedT>::value;
    }

    // @brief Returns true if all types [T, Ts...] are either equal to U or inherit from U.
    template<typename U>
    constexpr static inline bool all_are() {
        return std::is_base_of<U, DecayedT>::value
            && TypeChecker<Ts...>::template all_are<U>();
    }
    constexpr static const size_t count = TypeChecker<Ts...>::count + 1;
};

template<>
class TypeChecker<> {
public:
    template<typename U>
    constexpr static inline bool first_is_not() {
        return std::true_type::value;
    }
    template<typename U>
    constexpr static inline bool all_are() {
        return std::true_type::value;
    }
    constexpr static const size_t count = 0;
};

template<typename ... Ts>
TypeChecker<Ts...> make_type_checker(Ts ...) {
    return TypeChecker<Ts...>();
}

#include <type_traits>
#define ENABLE_IF(...) \
    typename = typename std::enable_if_t<__VA_ARGS__>

#define ENABLE_IF_SAME(a, b, type) \
    template<typename T = a> typename std::enable_if_t<std::is_same<T, b>::value, type>

template <class T, class M> M get_member_type(M T:: *);
#define GET_TYPE_OF(mem) decltype(get_member_type(mem))


//#include <type_traits>
// @brief Statically asserts that T is derived from type BaseType
#define EXPECT_TYPE(T, BaseType) static_assert(std::is_base_of<BaseType, typename std::decay<T>::type>::value || std::is_convertible<typename std::decay<T>::type, BaseType>::value, "expected template argument of type " #BaseType)
//#define EXPECT_TYPE(T, BaseType) static_assert(, "expected template argument of type " #BaseType)




template<typename TObj, typename TRet, typename ... TArgs>
class function_traits {
public:
    template<unsigned IUnpacked, typename ... TUnpackedArgs, ENABLE_IF(IUnpacked != sizeof...(TArgs))>
    static TRet invoke(TObj& obj, TRet(TObj::*func_ptr)(TArgs...), std::tuple<TArgs...> packed_args, TUnpackedArgs ... args) {
        return invoke<IUnpacked+1>(obj, func_ptr, packed_args, args..., std::get<IUnpacked>(packed_args));
    }

    template<unsigned IUnpacked>
    static TRet invoke(TObj& obj, TRet(TObj::*func_ptr)(TArgs...), std::tuple<TArgs...> packed_args, TArgs ... args) {
        return (obj.*func_ptr)(args...);
    }
};


/* @brief return_type<TypeList>::type represents the C++ native return type
* of a function returning 0 or more arguments.
*
* For an empty TypeList, the return type is void. For a list with
* one type, the return type is equal to that type. For a list with
* more than one items, the return type is a tuple.
*/
template<typename ... Types>
struct return_type;

template<>
struct return_type<> { typedef void type; };
template<typename T>
struct return_type<T> { typedef T type; };
template<typename T, typename ... Ts>
struct return_type<T, Ts...> { typedef std::tuple<T, Ts...> type; };



template<typename ... TInputsAndOutputs>
struct static_function_traits;

template<typename ... TInputs, typename ... TOutputs>
struct static_function_traits<std::tuple<TInputs...>, std::tuple<TOutputs...>> {
    using TRet = typename return_type<TOutputs...>::type;

    //template<TRet(*Function)(TInputs...), unsigned IUnpacked, typename ... TUnpackedInputs, ENABLE_IF(IUnpacked != sizeof...(TInputs))>
    //static std::tuple<TOutputs...> invoke(std::tuple<TInputs...> packed_args, TUnpackedInputs ... args) {
    //    return invoke<Function, IUnpacked+1>(packed_args, args..., std::get<IUnpacked>(packed_args));
    //}

    template<TRet(*Function)(TInputs...)>
    static std::tuple<TOutputs...> invoke(std::tuple<TInputs...>& packed_args) {
        return invoke_impl<Function>(packed_args, std::make_index_sequence<sizeof...(TInputs)>());
    }

    template<TRet(*Function)(TInputs...), size_t... Is>
    static std::tuple<TOutputs...> invoke_impl(std::tuple<TInputs...> packed_args, std::index_sequence<Is...>) {
        return invoke_impl_2<Function>(std::get<Is>(packed_args)...);
    }

    //template<TRet(*Function)(TInputs...)>
    //static std::enable_if_t<(sizeof...(TOutputs) == 0), std::tuple<TOutputs...>>
    template<TRet(*Function)(TInputs...), size_t IOutputs = sizeof...(TOutputs) /*, typename = typename std::enable_if_t<(IOutputs == 0)>>*/>
    static std::enable_if_t<(IOutputs == 0), std::tuple<TOutputs...>>
    invoke_impl_2(TInputs ... args) {
        Function(args...);
        return std::make_tuple<>();
    }

    //template<TRet(*Function)(TInputs...)>
    //static std::enable_if_t<(sizeof...(TOutputs) == 1), std::tuple<TOutputs...>>
    template<TRet(*Function)(TInputs...), size_t IOutputs = sizeof...(TOutputs) /*, typename = typename std::enable_if_t<(IOutputs == 1)>>*/>
    static std::enable_if_t<(IOutputs == 1), std::tuple<TOutputs...>>
    invoke_impl_2(TInputs ... args) {
        return std::make_tuple<TOutputs...>(Function(args...));
    }
//
//    template<TRet(*Function)(TInputs...), ENABLE_IF(sizeof...(TOutputs) >= 2)>
//    static /* std::enable_if_t<sizeof...(TOutputs) >= 2, */ std::tuple<TOutputs...> //>
//    invoke_impl_2(std::tuple<TInputs...> packed_args, TInputs ... args) {
//        return Function(args...);
//    }
};

/* @brief Invoke a class member function with a variable number of arguments that are supplied as a tuple

Example usage:

class MyClass {
public:
    int MyFunction(int a, int b) {
        return 0;
    }
};

MyClass my_object;
std::tuple<int, int> my_args(3, 4); // arguments are supplied as a tuple
int result = invoke_function_with_tuple(my_object, &MyClass::MyFunction, my_args);
*/
template<typename TObj, typename TRet, typename ... TArgs>
TRet invoke_function_with_tuple(TObj& obj, TRet(TObj::*func_ptr)(TArgs...), std::tuple<TArgs...> packed_args) {
    return function_traits<TObj, TRet, TArgs...>::template invoke<0>(obj, func_ptr, packed_args);
}

template<typename ... TOut, typename ... TIn, return_type<TOut...>(*Function)(TIn...)>
std::tuple<TOut...> invoke_with_tuples(std::tuple<TIn...> inputs) {
    static_function_traits<TOut..., TIn...>::template invoke<0>(inputs);
}



template<unsigned int... Ns>
struct sum;
template<>
struct sum<> { enum {value = 0}; };
template<unsigned size, unsigned... sizes>
struct sum<size, sizes...> { enum { value = size + sum<sizes...>::value }; };


// source: https://akrzemi1.wordpress.com/2017/05/18/asserts-in-constexpr-functions/
#if defined NDEBUG
# define X_ASSERT(CHECK) void(0)
#else
# define X_ASSERT(CHECK) \
    ( (CHECK) ? void(0) : []{assert(!#CHECK);}() )
#endif

template<class Fn, class Tuple, class>
struct for_each_in_tuple_result_impl;

template<class Fn, class Tuple, size_t... I>
struct for_each_in_tuple_result_impl<Fn, Tuple, std::index_sequence<I...>> {
    typedef std::tuple<decltype(std::forward<Fn>(std::declval<Fn>())(std::get<I>(std::declval<Tuple>())))...> type;
};

template<class Fn, class Tuple>
using for_each_in_tuple_result = for_each_in_tuple_result_impl<Fn, Tuple, std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>>;

template<class Fn, class Tuple>
using for_each_in_tuple_result_t = typename for_each_in_tuple_result<Fn, Tuple>::type;

template<class Fn, class Tuple, size_t... I>
for_each_in_tuple_result_t<Fn, Tuple> for_each_in_tuple_impl(Fn&& f, Tuple&& t, std::index_sequence<I...>) {
    return for_each_in_tuple_result_t<Fn, Tuple>(std::forward<Fn>(f)(std::get<I>(t))...);
}

template<class Fn, class Tuple>
for_each_in_tuple_result_t<Fn, Tuple> for_each_in_tuple(Fn&& f, Tuple&& t) {
    return for_each_in_tuple_impl(std::forward<Fn>(f), std::forward<Tuple>(t), std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{});
}
//template<class Fn, class Tuple>
//for_each_in_tuple_result_t<Fn, Tuple> for_each_in_tuple(Fn&& f, Tuple&& t) {
//    return 5;
//}


/* constexpr strings --------------------------------------------------------*/
/* adapted from:
* https://akrzemi1.wordpress.com/2017/06/28/compile-time-string-concatenation/
*/

template <size_t N>
class array_string
{
    char _array[N + 1];

    template<size_t... PACK>
    constexpr array_string(const char string_literal[N+1],
                           std::index_sequence<PACK...>)
    : _array{string_literal[PACK]..., '\0'}
    {
    }

    template<size_t N1, size_t... PACK1, size_t... PACK2>
    constexpr array_string(const array_string<N1>&     s1,
                           const array_string<N - N1>& s2,
                           std::index_sequence<PACK1...>,
                           std::index_sequence<PACK2...>)
    : _array{s1[PACK1]..., s2[PACK2]..., '\0'}
    {
    }

    template<size_t NOther, size_t... PACK>
    constexpr array_string(const array_string<NOther>& other,
                           std::index_sequence<PACK...>)
    : _array{other[PACK]..., '\0'}
    {
    }
 
public:
    constexpr char operator[](size_t i) const {
        return X_ASSERT(i >= 0 && i < N), _array[i];
    }
    
    constexpr std::size_t size() const {
        return N;
    }

    template<size_t N1, ENABLE_IF(N1 <= N)>
    constexpr array_string(const array_string<N1>& s1, const array_string<N - N1>& s2)
        : array_string{ s1, s2, std::make_index_sequence<N1>{},
                                std::make_index_sequence<N - N1>{} }
    {
    }

    constexpr array_string(const char string_literal[N+1])
        : array_string{ string_literal, std::make_index_sequence<N>{} }
    {
    }

//    template<size_t NOther, size_t START_IDX, size_t END_IDX, ENABLE_IF(END_IDX - START_IDX == N)>
//    constexpr array_string(const array_string<NOther>& other, std::index_sequence<START_IDX, END_IDX>)
//        : array_string{other, fibre::make_integer_sequence_from_to<size_t, START_IDX, END_IDX>{}}
//    {
//    }

    constexpr const char * c_str() const {
        return _array;
    }

    /**
     * @brief Returns the last index plus one of the specified char in the string
     * Returns 0 if the char was not found.
     */
    constexpr size_t after_last_index_of(char c, size_t idx = N) const {
        return idx == 0 ? 0 :
                (_array[idx] == c) ? (idx + 1) :
                after_last_index_of(c, idx - 1);
    }

    template<size_t IFrom, size_t ITo = N>
    constexpr array_string<ITo - IFrom> substring() const {
        return array_string<ITo - IFrom>(*this, fibre::make_integer_sequence_from_to<size_t, IFrom, ITo>{});
    }

    //constexpr array_string<N - last_index_of('/')> get_last_part(char c) {
    //    return substring<file_path.last_index_of('/')>();
    //}
};

// @brief Constructs a fixed length string from a string literal
template<size_t N_PLUS_1>
constexpr array_string<N_PLUS_1-1> make_const_string(const char (&string_literal)[N_PLUS_1]) {
    return array_string<N_PLUS_1-1>{string_literal};
}

// @brief Constructs a fixed length string by concatenating two strings
constexpr array_string<0> const_str_concat() {
    return array_string<0>("");
}

// @brief Constructs a fixed length string by concatenating two strings
template<size_t I1, size_t... ILengths>
constexpr array_string<I1+sum<ILengths...>::value> const_str_concat(const array_string<I1>& s1, const array_string<ILengths>& ... strings) {
    return array_string<I1+sum<ILengths...>::value>{s1, const_str_concat(strings...)};
}

// @brief Constructs a fixed length string by concatenating two strings
template<size_t IDelim>
constexpr array_string<0> const_str_join(const array_string<IDelim>& delimiter) {
    return array_string<0>("");
}

template<size_t IDelim, size_t I1>
constexpr array_string<I1> const_str_join(const array_string<IDelim>& delimiter, const array_string<I1>& s1) {
    return s1;
}

// @brief Constructs a fixed length string by concatenating two strings
template<size_t IDelim, size_t I1, size_t I2, size_t... ILengths>
constexpr array_string<I1+I2+sum<ILengths...>::value+sizeof...(ILengths)+1>
const_str_join(
    const array_string<IDelim>& delimiter,
    const array_string<I1> s1, const array_string<I2> s2, const array_string<ILengths>& ... strings) {
    return const_str_concat(s1, ",", const_str_join(s2, strings...));
}

// source: https://stackoverflow.com/questions/40159732/return-other-value-if-key-not-found-in-the-map
template<typename TKey, typename TValue>
TValue& get_or(std::unordered_map<TKey, TValue>& m, const TKey& key, TValue& default_value) {
    auto it = m.find(key);
    if (it == m.end()) {
        return default_value;
    } else {
        return it->second;
    }
}
template<typename TKey, typename TValue>
TValue* get_ptr(std::unordered_map<TKey, TValue>& m, const TKey& key) {
    auto it = m.find(key);
    if (it == m.end())
        return nullptr;
    else
        return &(it->second);
}

template <class T, std::size_t = sizeof(T)>
std::true_type is_complete_impl(T *);
std::false_type is_complete_impl(...);

/** @brief is_complete<T> resolves to std::true_type if T is complete
 * and to std::false_type otherwise. This can be used to check if a certain template
 * specialization exists.
 **/
template <class T>
using is_complete = decltype(is_complete_impl(std::declval<T*>()));

template<typename I, typename TRet, typename ... Ts>
struct dynamic_get_impl {
    template<typename TTuple>
    static TRet* get(size_t i, TTuple& t) {
        if (i == I::value)
            return &static_cast<TRet&>(std::get<I::value>(t));
        else if (i > I::value)
            return dynamic_get_impl<std::integral_constant<size_t, I::value + 1>, TRet, Ts...>::get(i, t);
        return nullptr; // this should not happen
    }
};

template<typename TRet, typename ... Ts>
struct dynamic_get_impl<std::integral_constant<size_t, sizeof...(Ts)>, TRet, Ts...> {
    static TRet* get(size_t i, const std::tuple<Ts...>& t) {
        return nullptr;
    }
};

template<typename TRet, typename ... Ts>
TRet* dynamic_get(size_t i, std::tuple<Ts...>& t) {
    return dynamic_get_impl<std::integral_constant<size_t, 0>, TRet, Ts...>::get(i, t);
}

template<typename TRet, typename ... Ts>
TRet* dynamic_get(size_t i, const std::tuple<Ts...>& t) {
    return dynamic_get_impl<std::integral_constant<size_t, 0>, TRet, Ts...>::get(i, t);
}

/* Simple Serializers/Deserializers -----------------------------------------*/

template<typename T, bool BigEndian, typename = void>
struct SimpleSerializer;
template<typename T>
using LittleEndianSerializer = SimpleSerializer<T, false>;
template<typename T>
using BigEndianSerializer = SimpleSerializer<T, true>;

//template<T, typename = Serializer<T>>
//using serializer_exists = std::true_t;




//template<typename T>
//std::true_type serializer_exists(SimpleSerializer<T, true> = SimpleSerializer<T, true>());

//template<typename T>
//std::false_type serializer_exists(SimpleSerializer<T, true>);

/* @brief Serializer/deserializer for arbitrary integral number types */
// TODO: allow reading an arbitrary number of bits
template<typename T, bool BigEndian>
struct SimpleSerializer<T, BigEndian, typename std::enable_if_t<std::is_integral<T>::value>> {
    static constexpr size_t BIT_WIDTH = std::numeric_limits<T>::digits;
    static constexpr size_t BYTE_WIDTH = (BIT_WIDTH + 7) / 8;

    static T read(const uint8_t buffer[BYTE_WIDTH]) {
        T result = 0;
        for (size_t i = 0; i < BYTE_WIDTH; i++) {
            const uint8_t byte = BigEndian ? buffer[i] : buffer[BYTE_WIDTH - i - 1];
            result <<= 8;
            result |= static_cast<T>(byte);
        }
        return result;
    }

    static void write(T value, uint8_t buffer[BYTE_WIDTH]) {
        for (size_t i = 0; i < BYTE_WIDTH; i++) {
            uint8_t byte = static_cast<uint8_t>(value & 0xff);
            if (!BigEndian) {
                buffer[i] = byte;
            } else {
                buffer[BYTE_WIDTH - i - 1] = byte;
            }
            value >>= 8;
        }
    }
};

/**
* @brief Serializer/deserializer for bool
*
* True is serialized as 1, false is serialized as 0.
* 0 is deserialized as false, all non-zero values are
* deserialized as true.
*/
template<bool BigEndian>
struct SimpleSerializer<bool, BigEndian, void> {
    static constexpr size_t BIT_WIDTH = 1;
    static constexpr size_t BYTE_WIDTH = (BIT_WIDTH + 7) / 8;

    static bool read(const uint8_t (&buffer)[1], bool* value) {
        return (buffer[0] != 0);
    }

    static void write(bool value, uint8_t (&buffer)[1]) {
        buffer[0] = value ? 1 : 0;
    }
};

/**
* @brief Writes a generic value to a buffer in little endian order.
* @param value The value to be serialized
* @param buffer Reference to a buffer of the exact correct size.
*/
template<typename T>
inline void write_le(T value, uint8_t (&buffer)[LittleEndianSerializer<T>::BYTE_WIDTH]) {
    static_assert(is_complete<LittleEndianSerializer<T>>(), "no LittleEndianSerializer is defined for type T");
    LittleEndianSerializer<T>::write(value, buffer);
}

/**
* @brief Writes a value to a buffer in big endian order.
* @param value The value to be serialized
* @param buffer Reference to a buffer of the exact correct size.
*/
template<typename T>
inline void write_be(T value, uint8_t (&buffer)[BigEndianSerializer<T>::BYTE_WIDTH]) {
    static_assert(is_complete<BigEndianSerializer<T>>(), "no BigEndianSerializer is defined for type T");
    BigEndianSerializer<T>::write(value, buffer);
}

/**
* @brief Writes a value to a buffer in little endian order.
* @param value The value to be serialized
* @param buffer A pointer to the buffer where to write the value
* @param length The length of the output buffer.
*        If not provided, it is assumed that the output buffer is sufficiently large.
* @param written_bytes If not null, this variable is incremented by the number of
*        bytes that were written to the output buffer.
* @retval true The value was written successfully.
* @retval false The length of the output buffer was too small.
*        In this case neither the output buffer nor written_bytes are modified.
*/
template<typename T>
inline bool write_le(T value, uint8_t* buffer, size_t length /*= LittleEndianSerializer<T>::BYTE_WIDTH*/, size_t* written_bytes = nullptr) {
    static_assert(is_complete<LittleEndianSerializer<T>>(), "no LittleEndianSerializer is defined for type T");
    if (length < LittleEndianSerializer<T>::BYTE_WIDTH)
        return false;
    LittleEndianSerializer<T>::write(value, buffer);
    if (written_bytes)
        *written_bytes += LittleEndianSerializer<T>::BYTE_WIDTH;
    return true;
}

/**
* @brief Writes a value to a buffer in big endian order.
* @copydetails write_le
*/
template<typename T>
inline bool write_be(T value, uint8_t* buffer, size_t length = BigEndianSerializer<T>::BYTE_WIDTH, size_t* written_bytes = nullptr) {
    if (length < BigEndianSerializer<T>::BYTE_WIDTH)
        return false;
    BigEndianSerializer<T>::write(value, buffer);
    if (written_bytes)
        *written_bytes += BigEndianSerializer<T>::BYTE_WIDTH;
    return true;
}

/**
* @brief Reads a generic value from a buffer in little endian order.
* @param buffer Reference to a buffer of the exact correct size.
* @returns The value that was read.
*/
template<typename T>
T read_le(const uint8_t (&buffer)[LittleEndianSerializer<T>::BYTE_WIDTH]) {
    static_assert(is_complete<LittleEndianSerializer<T>>(), "no LittleEndianSerializer is defined for type T");
    return LittleEndianSerializer<T>::read(buffer);
}

/**
* @brief Reads a generic value from a buffer in big endian order.
* @param buffer Reference to a buffer of the exact correct size.
* @returns The value that was read.
*/
template<typename T>
T read_be(const uint8_t (&buffer)[BigEndianSerializer<T>::BYTE_WIDTH]) {
    static_assert(is_complete<BigEndianSerializer<T>>(), "no BigEndianSerializer is defined for type T");
    return BigEndianSerializer<T>::read(buffer);
}

/**
* @brief Reads a generic value from a buffer in little endian order.
* @param buffer Pointer to the first byte that should be read.
* @param length Length of the buffer to be read from.
*        If not provided, it is assumed that the buffer is sufficiently large.
* @param value Pointer to the variable where the deserialized value will be stored.
* @param read_bytes If not null, this variable is incremented by the number
*        bytes that were read. This variable is incremented even if the value pointer is null.
* @retval true The value was read successfully.
* @retval false The length of the output buffer was too small.
*        In this case neither value nor read_bytes are modified.
*/
template<typename T>
bool read_le(const uint8_t * buffer, size_t length = LittleEndianSerializer<T>::BYTE_WIDTH, T* value = nullptr, size_t* read_bytes = nullptr) {
    static_assert(is_complete<LittleEndianSerializer<T>>(), "no LittleEndianSerializer is defined for type T");
    if (length < LittleEndianSerializer<T>::BYTE_WIDTH)
        return false;
    if (value)
        *value = LittleEndianSerializer<T>::read(buffer);
    if (read_bytes)
        *read_bytes += LittleEndianSerializer<T>::BYTE_WIDTH;
    return true;
}

/**
* @brief Reads a generic value from a buffer in little endian order.
* @param buffer_ptr Pointer to a variable holding the pointer to the first byte that should be read.
*        The variable is incremented by the number of bytes read.
*        If the buffer is too small, this variable is not modified.
* @param length_ptr Pointer to a variable holding the length of the buffer to be read from.
*        The variable is decremented by the number of bytes read.
*        If the buffer is too small, this variable is not modified.
* @returns The value that was read.
*        If the buffer is too small, the default constructor is used to generate a return value.
*/
template<typename T>
T read_le(const uint8_t** buffer_ptr, size_t* length_ptr) {
    static_assert(is_complete<LittleEndianSerializer<T>>(), "no LittleEndianSerializer is defined for type T");
    if (*length_ptr < LittleEndianSerializer<T>::BYTE_WIDTH)
        return T();
    T result = LittleEndianSerializer<T>::read(*buffer_ptr);
    *buffer_ptr += LittleEndianSerializer<T>::BYTE_WIDTH;
    *length_ptr -= LittleEndianSerializer<T>::BYTE_WIDTH;
    return result;
}

/**
* @brief Reads a generic value from a buffer in little endian order.
* @copydetails read_le
*/
template<typename T>
T read_be(const uint8_t** buffer_ptr, size_t* length_ptr) {
    static_assert(is_complete<BigEndianSerializer<T>>(), "no BigEndianSerializer is defined for type T");
    if (*length_ptr < BigEndianSerializer<T>::BYTE_WIDTH)
        return T();
    T result = BigEndianSerializer<T>::read(*buffer_ptr);
    *buffer_ptr += BigEndianSerializer<T>::BYTE_WIDTH;
    *length_ptr -= BigEndianSerializer<T>::BYTE_WIDTH;
    return result;
}


/* Hex to numbers ------------------------------------------------------------*/

template<typename T>
constexpr size_t hex_digits() {
    return (std::numeric_limits<T>::digits + 3) / 4;
}

/* @brief Converts a hexadecimal digit to a uint8_t.
* @param output If not null, the digit's value is stored in this output
* Returns true if the char is a valid hex digit, false otherwise
*/
static bool hex_digit_to_byte(char ch, uint8_t* output) {
    uint8_t nil_output = 0;
    if (!output)
        output = &nil_output;
    if (ch >= '0' && ch <= '9')
        return (*output) = ch - '0', true;
    if (ch >= 'a' && ch <= 'f')
        return (*output) = ch - 'a' + 10, true;
    if (ch >= 'A' && ch <= 'F')
        return (*output) = ch - 'A' + 10, true;
    return false;
}

/* @brief Converts a hex string to an integer
* @param output If not null, the result is stored in this output
* Returns true if the string represents a valid hex value, false otherwise.
*/
template<typename TInt>
bool hex_string_to_int(const char * str, size_t length, TInt* output) {
    constexpr size_t N_DIGITS = hex_digits<TInt>();
    TInt result = 0;
    if (length > N_DIGITS)
        length = N_DIGITS;
    for (size_t i = 0; i < length && str[i]; i++) {
        uint8_t digit = 0;
        if (!hex_digit_to_byte(str[i], &digit))
            return false;
        result <<= 4;
        result += digit;
    }
    if (output)
        *output = result;
    return true;
}

template<typename TInt>
bool hex_string_to_int(const char * str, TInt* output) {
    return hex_string_to_int<TInt>(str, hex_digits<TInt>(), output);
}

template<typename TInt, size_t ICount>
bool hex_string_to_int_arr(const char * str, size_t length, TInt (&output)[ICount]) {
    for (size_t i = 0; i < ICount; i++) {
        if (!hex_string_to_int<TInt>(&str[i * hex_digits<TInt>()], &output[i]))
            return false;
    }
    return true;
}

template<typename TInt, size_t ICount>
bool hex_string_to_int_arr(const char * str, TInt (&output)[ICount]) {
    return hex_string_to_int_arr(str, hex_digits<TInt>() * ICount, output);
}

namespace fibre {

template<typename T>
class HexPrinter {
public:
    HexPrinter(T val) : val_(val) {}
    T val_;
};

template<typename T>
std::ostream& operator<<(std::ostream& stream, const HexPrinter<T>& printer) {
    // TODO: specialize for char
    return stream << std::hex << std::setw(hex_digits<T>()) << std::setfill('0') << static_cast<uint64_t>(printer.val_);
}

template<typename T>
HexPrinter<T> as_hex(T val) { return HexPrinter<T>(val); }

template<typename T>
class HexPrinter<T*> {
public:
    HexPrinter(T* ptr, size_t length) : ptr_(ptr), length_(length) {}
    T* ptr_;
    size_t length_;
};

template<typename T>
std::ostream& operator<<(std::ostream& stream, const HexPrinter<T*>& printer) {
    for (size_t pos = 0; pos < printer.length_; ++pos) {
        stream << " " << as_hex(printer.ptr_[pos]);
        if (((pos + 1) % 16) == 0)
            stream << std::endl;
    }
    return stream;
}

template<typename T, size_t ILength>
HexPrinter<T*> as_hex(T (&val)[ILength]) { return HexPrinter<T*>(val, ILength); }

}


template<typename TDereferenceable, typename TResult>
class simple_iterator : std::iterator<std::random_access_iterator_tag, TResult> {
    TDereferenceable *container_;
    size_t i_;
public:
    using reference = TResult;
    explicit simple_iterator(TDereferenceable& container, size_t pos) : container_(&container), i_(pos) {}
    simple_iterator& operator++() { ++i_; return *this; }
    simple_iterator operator++(int) { simple_iterator retval = *this; ++(*this); return retval; }
    bool operator==(simple_iterator other) const { return (container_ == other.container_) && (i_ == other.i_); }
    bool operator!=(simple_iterator other) const { return !(*this == other); }
    bool operator<(simple_iterator other) const { return i_ < other.i_; }
    bool operator>(simple_iterator other) const { return i_ > other.i_; }
    bool operator<=(simple_iterator other) const { return (*this < other) || (*this == other); }
    bool operator>=(simple_iterator other) const { return (*this > other) || (*this == other); }
    TResult operator*() const { return (*container_)[i_]; }
};


#endif // __CPP_UTILS_HPP
