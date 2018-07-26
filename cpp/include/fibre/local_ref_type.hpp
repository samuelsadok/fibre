#ifndef __FIBRE_LOCAL_REF_TYPE
#define __FIBRE_LOCAL_REF_TYPE

//#ifndef __FIBRE_HPP
//#error "This file should not be included directly. Include fibre.hpp instead."
//#endif
#include <fibre/fibre.hpp>

namespace fibre {

class LocalRefType {
public:
    //virtual std::tuple<LocalRefType*, size_t> get_property(size_t index) = 0;
    virtual uint16_t get_hash() = 0;
    virtual bool get_as_json(const char ** output, size_t* length) = 0;
};


/**
 * @brief Assembles a JSON snippet that describes a function
 */
template<typename TMetadata, TMetadata& metadata>
struct RefTypeJSONAssembler {
    template<size_t I>
    //static constexpr static_string<10 + sizeof(std::get<I>(metadata.get_property_names()))/sizeof(std::get<I>(metadata.get_property_names())[0])>
    static constexpr static_string<11 + std::get<I>(metadata.get_property_names()).size()>
    get_property_json() {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            std::get<I>(metadata.get_property_names()),
            make_const_string("\"}")
        );
    }

    template<size_t... Is>
    static constexpr auto get_all_properties_json(std::index_sequence<Is...>)
        -> decltype(const_str_join(std::declval<static_string<1>>(), get_property_json<Is>()...)) {
        return const_str_join(make_const_string(","), get_property_json<Is>()...);
    }

    // @brief Returns a JSON snippet that describes this function
    static bool get_as_json(const char ** output, size_t* length) {
        static const constexpr auto json = const_str_concat(
            make_const_string("{\"name\":\""),
            metadata.get_type_name(),
            make_const_string("\",\"properties\":["),
            get_all_properties_json(std::make_index_sequence<TMetadata::NProperties>()),
            make_const_string("]}")
        );
        //memcpy(output, json.c_str(), std::min((size_t)256, sizeof(json)));
        if (output)
            *output = json.c_str();
        if (length)
            *length = sizeof(json);
        return true;
    }
};

template<typename TTypeName, typename TPropNames>
struct StaticRefTypeMetadata;

template<size_t IFun, size_t... IProp>
struct StaticRefTypeMetadata<
        static_string<IFun>,
        static_string_arr<IProp...>> {
    using TTypeName = static_string<IFun>;
    using TPropertyNames = static_string_arr<IProp...>;
    TTypeName type_name;
    TPropertyNames property_names;

    constexpr StaticRefTypeMetadata(TTypeName type_name, TPropertyNames property_names)
        : type_name(type_name), property_names(property_names) {}

    template<size_t ... Is>
    constexpr StaticRefTypeMetadata<TTypeName, static_string_arr<Is...>> with_properties(const char (&...names)[Is]) {
        return StaticRefTypeMetadata<TTypeName, static_string_arr<Is...>>(
            type_name,
            std::tuple_cat(property_names, std::tuple<const char (&)[Is]...>(names...)));
    }

    static constexpr const size_t NProperties = (sizeof...(IProp));
    constexpr TTypeName get_type_name() { return type_name; }
    constexpr TPropertyNames get_property_names() { return property_names; }
};

template<size_t IType>
static constexpr StaticRefTypeMetadata<static_string<IType>, static_string_arr<>> make_ref_type_props(const char (&type_name)[IType]) {
    return StaticRefTypeMetadata<static_string<IType>, static_string_arr<>>(static_string<IType>(type_name), empty_static_string_arr);
}



template<
    typename T,
    typename TMetadata, TMetadata& metadata>
class StaticLocalRefType : public LocalRefType {

    uint16_t get_hash() {
        // TODO: implement
        return 0;
    }

    // @brief Returns a JSON snippet that describes this type
    bool get_as_json(const char ** output, size_t* length) final {
        return RefTypeJSONAssembler<TMetadata, metadata>::get_as_json(output, length);
    }
};

}

#endif // __FIBRE_LOCAL_REF_TYPE
