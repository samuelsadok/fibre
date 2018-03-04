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
};

#define ENABLE_IF(...) \
  typename std::enable_if<__VA_ARGS__>::type* = nullptr


template <class T, class M> M get_member_type(M T:: *);
#define GET_TYPE_OF(mem) decltype(get_member_type(mem))


// @brief Statically asserts that T is derived from type BaseType
// (currently unused)
#define EXPECT_TYPE(T, BaseType) static_assert(std::is_base_of<BaseType, T>::value, "expected template argument of type " #BaseType)
