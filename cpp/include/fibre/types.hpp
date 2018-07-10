#ifndef __FIBRE_TYPES_HPP
#define __FIBRE_TYPES_HPP

#include "encoders.hpp"
#include "decoders.hpp"

#include <vector>

#include <stdint.h>


// utils
/*
template<typename ... Ts>
constexpr size_t get_element_count();

template<>
constexpr size_t get_element_count<>() {
    return 0;
}

template<typename T, typename ... Ts>
constexpr size_t get_element_count<T, Ts...>() {
    return get_element_count<Ts...>() + 1;
}


template<typename ... Ts>
constexpr size_t get_element_count();

template<>
constexpr size_t get_element_count<>() {
    return 0;
}

template<typename T, typename ... Ts>
constexpr size_t get_element_count<T, Ts...>() {
    return get_element_count<Ts...>() + 1;
}*/


namespace fibre {

template<typename T>
using FibreList = std::vector<T>;

class FibreRefType {
public:
    virtual std::tuple<FibreRefType*, size_t> get_property(size_t index) = 0;
};

//class FibreTypeType : public FibreType {
//    std::tuple<FibreType*, size_t> get_member(size_t index) {
//        return std::make_tuple<FibreType*, size_t>(nullptr, 0); // no members
//    }
//}

template<typename T>
T& global_instance_of() {
    static T instance = T();
    return instance;
}

extern int the_one_and_only_int;

class ObjectReference_t {
public:
    ObjectReference_t(ObjectReference_t* parent, uintptr_t obj, FibreRefType* type) :
        obj(obj), type(type) {}
    ObjectReference_t dereference(size_t index) {
        return ObjectReference_t(
            this,
            obj + std::get<1>(type->get_property(index)),
            std::get<0>(type->get_property(index)));
    }
    ObjectReference_t *parent; // pointer to the parent object reference
    uintptr_t obj; // context pointer (meaning depends on type)
    FibreRefType* type;

    //static ObjectReference_t root() { return ObjectReference_t(
    //    nullptr,
    //    (uintptr_t)&the_one_and_only_int,
    //    &global_instance_of<FibreRefType<int>>()); }
};


template<typename T>
class IntNumberType : FibreRefType {
    // @brief Statically known encoders
    typedef std::tuple<
        VarintStreamEncoder<T>
        //FixedIntStreamEncoder<T>,
    > static_encoders;
    // @brief Statically known decoders
    typedef std::tuple<
        VarintDecoder<T>
        //FixedIntStreamDecoder<T>,
    > static_decoders;
    typedef std::tuple_element_t<0, static_encoders> default_encoder;
    typedef std::tuple_element_t<0, static_decoders> default_decoder;
};

template<typename T>
class fibre_type;

template<>
class fibre_type<int32_t> { typedef IntNumberType<int32_t> type; };
template<>
class fibre_type<uint32_t> { typedef IntNumberType<uint32_t> type; };

template<typename T>
using fibre_type_t = typename fibre_type<T>::type;

}

#define FIBRE_PROPERTY(name) \
    std::make_tuple<const char *, fibre::FibreRefType*, size_t>( \
        #name, \
        &fibre::global_instance_of<FibreRefType<decltype(std::declval<underlying_type>().name)>>(), \
        offsetof(underlying_type, name)) \

template<typename T>
class FibreRefType;

#define FIBRE_EXPORT_TYPE(class_name, ...) \
template<> \
class FibreRefType<class_name> : public fibre::FibreRefType { \
public: \
    typedef class_name underlying_type; \
    constexpr static const size_t num_properties = decltype(make_type_checker(__VA_ARGS__))::count; \
    static const std::tuple<const char *, fibre::FibreRefType*, size_t> properties[num_properties]; \
    std::tuple<fibre::FibreRefType*, size_t> get_property(size_t index) final { \
        if (index < num_properties) { \
            fibre::FibreRefType* t = std::get<1>(properties[index]); \
            size_t s = std::get<2>(properties[index]); \
            return std::make_tuple<fibre::FibreRefType*, size_t>( \
                    std::forward<fibre::FibreRefType*>(t), \
                    std::forward<size_t>(s)); \
        } else \
            return std::make_tuple<fibre::FibreRefType*, size_t>(nullptr, 0); /* no members */ \
    } \
}; \
const std::tuple<const char *, fibre::FibreRefType*, size_t> FibreRefType<class_name>::properties[] = { \
    __VA_ARGS__ \
}

#endif
