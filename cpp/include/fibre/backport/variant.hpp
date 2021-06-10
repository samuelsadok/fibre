#ifndef __FIBRE_BACKPORT_VARIANT_HPP
#define __FIBRE_BACKPORT_VARIANT_HPP

#if __cplusplus >= 201703L
#include <variant>
#else

namespace std {

namespace detail {

template<typename TQuery, typename T, typename ... Ts>
struct index_of : integral_constant<size_t, (index_of<TQuery, Ts...>::value + 1)> {};

template<typename TQuery, typename ... Ts>
struct index_of<TQuery, TQuery, Ts...> : integral_constant<size_t, 0> {};

}

template<typename T>
struct identity { using type = T; };

template<typename ... Ts>
struct overload_resolver;

template<>
struct overload_resolver<> { void operator()() const; };

template<typename T, typename ... Ts>
struct overload_resolver<T, Ts...> : overload_resolver<Ts...> {
    using overload_resolver<Ts...>::operator();
    identity<T> operator()(T) const;
};

/**
 * @brief Heavily simplified version of the C++17 std::variant.
 * Whatever compiles should work as one would expect from the C++17 variant.
 */
template<typename ... Ts>
class variant;

// Empty variant is ill-formed. Only used for clean recursion here.
template<>
class variant<> {
public:
    using storage_t = char[0];
    storage_t content_;

    static void selective_destructor(char* storage, size_t index) {
#if _HAS_EXCEPTIONS != 0
        throw;
#endif
    }

    static void selective_copy_constuctor(char* target, const char* source, size_t index) {
#if _HAS_EXCEPTIONS != 0
        throw;
#endif
    }

    static bool selective_eq(const char* lhs, const char* rhs, size_t index) {
#if _HAS_EXCEPTIONS != 0
        throw;
#else
        return false;
#endif
    }

    static bool selective_neq(const char* lhs, const char* rhs, size_t index) {
#if _HAS_EXCEPTIONS != 0
        throw;
#else
        return true;
#endif
    }

    template<typename TFunc, typename ... TArgs>
    static void selective_invoke_const(const char* content, size_t index, TFunc functor, TArgs&&... args) {
#if _HAS_EXCEPTIONS != 0
        throw;
#endif
    }

    template<typename TFunc, typename ... TArgs>
    static void selective_invoke(const char* content, size_t index, TFunc functor, TArgs&&... args) {
#if _HAS_EXCEPTIONS != 0
        throw;
#endif
    }
};

template<typename T, typename ... Ts>
class variant<T, Ts...> {
public:
    using storage_t = char[sizeof(T) > sizeof(typename variant<Ts...>::storage_t) ? sizeof(T) : sizeof(typename variant<Ts...>::storage_t)];

    static void selective_copy_constuctor(char* target, const char* source, size_t index) {
        if (index == 0) {
            new ((T*)target) T{*(T*)source}; // in-place construction using first type's copy constructor
        } else {
            variant<Ts...>::selective_copy_constuctor(target, source, index - 1);
        }
    }

    static void selective_destructor(char* storage, size_t index) {
        if (index == 0) {
            ((T*)storage)->~T();
        } else {
            variant<Ts...>::selective_destructor(storage, index - 1);
        }
    }

    static bool selective_eq(const char* lhs, const char* rhs, size_t index) {
        if (index == 0) {
            return ((*(T*)lhs) == (*(T*)rhs));
        } else {
            return variant<Ts...>::selective_eq(lhs, rhs, index - 1);
        }
    }

    static bool selective_neq(const char* lhs, const char* rhs, size_t index) {
        if (index == 0) {
            return ((*(T*)lhs) != (*(T*)rhs));
        } else {
            return variant<Ts...>::selective_neq(lhs, rhs, index - 1);
        }
    }

    template<typename TFunc, typename ... TArgs>
    static void selective_invoke_const(const char* content, size_t index, TFunc functor, TArgs&&... args) {
        if (index == 0) {
            functor(*(T*)content, std::forward<TArgs>(args)...);
        } else {
            variant<Ts...>::selective_invoke_const(content, index - 1, functor, std::forward<TArgs>(args)...);
        }
    }

    template<typename TFunc, typename ... TArgs>
    static void selective_invoke(char* content, size_t index, TFunc functor, TArgs&&... args) {
        if (index == 0) {
            functor(*(T*)content, std::forward<TArgs>(args)...);
        } else {
            variant<Ts...>::selective_invoke(content, index - 1, functor, std::forward<TArgs>(args)...);
        }
    }

    variant() : index_(0) {
        new ((T*)content_) T{}; // in-place construction using first type's default constructor
    }

    variant(const variant & other) : index_(other.index_) {
        selective_copy_constuctor(content_, other.content_, index_);
    }

    variant(variant&& other) : index_(other.index_) {
        // TODO: implement
        selective_copy_constuctor(content_, other.content_, index_);
    }

    // Find the best match out of `T, Ts...` with `TArg` as the argument.
    template<typename TArg>
    using best_match = decltype(overload_resolver<T, Ts...>()(std::declval<TArg>()));

    template<class TArg, typename TTarget = typename best_match<TArg&&>::type> //, typename=typename std::enable_if_t<!(std::is_same<std::decay_t<U>, variant>::value)>, typename TTarget=decltype(indicator_func(std::forward<U>(std::declval<U>()))), typename TIndex=index_of<TTarget, T, Ts...>>
    variant(TArg&& arg) {
        new ((TTarget*)content_) TTarget{std::forward<TArg>(arg)};
        index_ = detail::index_of<TTarget, T, Ts...>::value;
    }

    ~variant() {
        selective_destructor(content_, index_);
    }

    inline variant& operator=(const variant & other) {
        selective_destructor(content_, index_);
        index_ = other.index_;
        selective_copy_constuctor(content_, other.content_, index_);
        return *this;
    }

    inline bool operator==(const variant& rhs) const {
        return (index_ == rhs.index_) && selective_eq(this->content_, rhs.content_, index_);
    }

    inline bool operator!=(const variant& rhs) const {
        return (index_ != rhs.index_) || selective_neq(this->content_, rhs.content_, index_);
    }

    template<typename TFunc, typename ... TArgs>
    void invoke(TFunc functor, TArgs&&... args) const {
        selective_invoke_const(content_, index_, functor, std::forward<TArgs>(args)...);
    }

    template<typename TFunc, typename ... TArgs>
    void invoke(TFunc functor, TArgs&&... args) {
        selective_invoke(content_, index_, functor, std::forward<TArgs>(args)...);
    }

    storage_t content_;
    size_t index_;

    size_t index() const { return index_; }
};

template<size_t I, typename ... Ts>
typename tuple_element<I, tuple<Ts...>>::type& get(const std::variant<Ts...>& val) {
#if _HAS_EXCEPTIONS != 0
    if (val.index() != I) {
        throw;
    }
#endif
    using T = typename tuple_element<I, tuple<Ts...>>::type;
    return *((T*)val.content_);
}

template<typename T, typename ... Ts>
T& get(std::variant<Ts...>& val) {
    constexpr size_t index = detail::index_of<T, Ts...>::value;
    return std::get<index>(val);
}

template<typename T> struct variant_size;
template<typename... Ts> struct variant_size<std::variant<Ts...>>
    : std::integral_constant<std::size_t, sizeof...(Ts)> {};

}

#endif

#endif // __FIBRE_BACKPORT_VARIANT_HPP
