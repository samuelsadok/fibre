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
    virtual uint16_t get_hash() const = 0;
    virtual bool get_as_json(const char ** output, size_t* length) const = 0;
};


/**
 * @brief Assembles a JSON snippet that describes a function
 */
template<typename TMetadata>
struct RefTypeJSONAssembler {
private:
    template<size_t I>
    using get_property_json_t = static_string<11 + std::tuple_element_t<I, typename TMetadata::TPropertyNames>::size()>;

    template<size_t I>
    static constexpr get_property_json_t<I>
    get_property_json(const TMetadata& metadata) {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            std::get<I>(metadata.get_property_names()),
            make_const_string("\"}")
        );
    }

    template<typename Is>
    struct get_all_properties_json_type;
    template<size_t... Is>
    struct get_all_properties_json_type<std::index_sequence<Is...>> {
        using type = const_str_join_t<1, get_property_json_t<Is>::size()...>;
    };
    using get_all_properties_json_t = typename get_all_properties_json_type<std::make_index_sequence<TMetadata::NProperties>>::type;

    template<size_t... Is>
    static constexpr get_all_properties_json_t
    get_all_properties_json(const TMetadata& metadata, std::index_sequence<Is...>) {
        return const_str_join(make_const_string(","), get_property_json<Is>(metadata)...);
    }

public:
    using get_json_t = static_string<27 + TMetadata::TTypeName::size() + get_all_properties_json_t::size()>;

    // @brief Returns a JSON snippet that describes this type
    static constexpr get_json_t
    get_as_json(const TMetadata& metadata) {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            metadata.get_type_name(),
            make_const_string("\",\"properties\":["),
            get_all_properties_json(metadata, std::make_index_sequence<TMetadata::NProperties>()),
            make_const_string("]}")
        );
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
    typename TMetadata>
class StaticLocalRefType : public LocalRefType {
public:
    constexpr StaticLocalRefType(const char * json, size_t json_length) :
        json_(json),
        json_length_(json_length) {}

    uint16_t get_hash() const final {
        // TODO: implement
        return 0;
    }

    // @brief Returns a JSON snippet that describes this type
    bool get_as_json(const char ** output, size_t* length) const final {
        if (output) *output = json_;
        if (length) *length = json_length_;
        return true;
    }

private:
    const char * json_;
    size_t json_length_;
};

template<typename T, typename TMetadata>
StaticLocalRefType<T, TMetadata> make_local_ref_type(const TMetadata metadata) {
    static const auto json = RefTypeJSONAssembler<TMetadata>::get_as_json(metadata);
    using ret_t = StaticLocalRefType<T, TMetadata>;
    return ret_t(json.c_str(), decltype(json)::size());
};

}

#endif // __FIBRE_LOCAL_REF_TYPE
