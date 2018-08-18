#ifndef __FIBRE_LOCAL_REF_TYPE
#define __FIBRE_LOCAL_REF_TYPE

//#ifndef __FIBRE_HPP
//#error "This file should not be included directly. Include fibre.hpp instead."
//#endif
#include <fibre/fibre.hpp>

namespace fibre {

class UnimplementedRefType;
class RootType;

template<typename T>
struct fibre_type {
    using type = UnimplementedRefType;
};

template<typename T>
using fibre_type_t = typename fibre_type<T>::type;


class LocalRefType {
public:
    //virtual std::tuple<LocalRefType*, size_t> get_property(size_t index) = 0;
    virtual ObjectReference_t dereference(ObjectReference_t* ref, size_t index) const = 0;
    virtual uint16_t get_hash() const = 0;
    virtual bool get_as_json(const char ** output, size_t* length) const = 0;
};

class RootType : public LocalRefType {
public:
    ObjectReference_t dereference(ObjectReference_t* ref, size_t index) const final;
    uint16_t get_hash() const final { return 0; }
    bool get_as_json(const char ** output, size_t* length) const final {
        if (output) *output = "__root_type__";
        if (length) *length = sizeof("__root_type__") - 1;
        return true;
    }
};

class ObjectReference_t {
public:
    template<typename T>
    ObjectReference_t(T* obj) :
        obj_(obj), type_(&global_instance_of<fibre_type_t<T>>()) {}

    ObjectReference_t(void* obj, LocalRefType* type) :
        obj_(obj), type_(type) {}

    ObjectReference_t dereference(size_t index) {
        if (type_) {
            return type_->dereference(this, index);
        } else {
            return nil();
        }
    }
    //ObjectReference_t *parent; // pointer to the parent object reference
    void* obj_; // context pointer (meaning depends on type)
    LocalRefType* type_;
    
    static ObjectReference_t nil() {
        return ObjectReference_t(nullptr, nullptr);
    }

    static ObjectReference_t root() {
        return ObjectReference_t(nullptr, &global_instance_of<RootType>());
    }
};

class UnimplementedRefType : public LocalRefType {
public:
    ObjectReference_t dereference(ObjectReference_t* ref, size_t index) const final { return ObjectReference_t::nil(); }
    uint16_t get_hash() const final { return 0; }
    bool get_as_json(const char ** output, size_t* length) const final {
        if (output) *output = "__empty_type__";
        if (length) *length = sizeof("__empty_type__") - 1;
        return true;
    }
private:
    //static constexpr const char json[] = "__empty_type__";
};



template<typename TObj>
class ObjectReferenceDecoder : public StreamRepeater<FixedIntDecoder<uint32_t, false>> {
public:
    //operator StreamSink& () { return *this; }
    //operator const StreamSink& () const { return *this; }

    TObj* get_value() const {
        return nullptr;
    }

private:
    bool advance_state() final {
        if (stream_sink_.get_value()) {
            obj = obj.dereference(stream_sink_.get_value());
            return true;
        } else {
            return false;
        }
    }
    ObjectReference_t obj = ObjectReference_t::root();
};





/**
 * @brief Assembles a JSON snippet that describes a function
 */
template<typename TMetadata>
struct RefTypeJSONAssembler {
private:
    template<size_t I>
    using get_property_json_t = static_string<11 + std::tuple_element_t<1, std::tuple_element_t<I, typename TMetadata::TPropertyMetadata>>::size()>;

    template<size_t I>
    static constexpr get_property_json_t<I>
    get_property_json(const TMetadata& metadata) {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            std::get<1>(std::get<I>(metadata.get_property_metadata())),
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

struct property_metadata_item_tag {};

template<size_t INameLength, typename TProp, typename TObj>
using PropertyMetadata = std::tuple<property_metadata_item_tag, static_string<INameLength>, TProp TObj::*>;

template<size_t INameLengthPlus1, typename TProp, typename TObj>
constexpr PropertyMetadata<(INameLengthPlus1-1), TProp, TObj> make_property_metadata(const char (&name)[INameLengthPlus1], TProp TObj::* prop) {
    return PropertyMetadata<(INameLengthPlus1-1), TProp, TObj>(
        property_metadata_item_tag(),
        make_const_string(name),
        std::forward<TProp TObj::*>(prop));
}


using RectifiedPropertyMetadata = std::tuple<LocalRefType*, size_t>;

template<size_t INameLength, typename TProp, typename TObj>
RectifiedPropertyMetadata rectify_metadata(PropertyMetadata<INameLength, TProp, TObj> prop) {
    auto offset = reinterpret_cast<size_t>(&(reinterpret_cast<TObj*>(0)->*std::get<2>(prop)));
    return RectifiedPropertyMetadata(
        &global_instance_of<fibre_type_t<TProp>>(),
        offset);
}

template<typename... Ts, size_t... Is>
std::array<RectifiedPropertyMetadata, sizeof...(Is)> rectify_all_metadata(std::tuple<Ts...> props, std::index_sequence<Is...>) {
    return { rectify_metadata(std::get<Is>(props))... };
}

template<typename TTypeName, typename TPropNames>
struct StaticRefTypeMetadata;

template<size_t IFun, typename... TProperties>
struct StaticRefTypeMetadata<
        static_string<IFun>,
        std::tuple<TProperties...>> {
    static constexpr const size_t NProperties = (sizeof...(TProperties));
    using TTypeName = static_string<IFun>;
    using TPropertyMetadata = std::tuple<TProperties...>;

    TTypeName type_name;
    TPropertyMetadata property_metadata;

    constexpr StaticRefTypeMetadata(TTypeName type_name, TPropertyMetadata property_metadata)
        : type_name(type_name), property_metadata(property_metadata) {}

    /*template<size_t ... Is>
    constexpr StaticRefTypeMetadata<TTypeName, static_string_arr<(Is-1)...>> with_properties(const char (&...names)[Is], TArgs TObj::* ... props) {
        return StaticRefTypeMetadata<TTypeName, static_string_arr<(Is-1)...>>(
            type_name,
            std::tuple_cat(property_names, std::tuple<const char (&)[Is]...>(names...)));
    }*/

    template<size_t INameLength, typename TProp, typename TObj>
    constexpr StaticRefTypeMetadata<TTypeName, tuple_cat_t<TPropertyMetadata, std::tuple<PropertyMetadata<INameLength, TProp, TObj>>>> with_item(PropertyMetadata<INameLength, TProp, TObj> prop) {
        return StaticRefTypeMetadata<TTypeName, tuple_cat_t<TPropertyMetadata, std::tuple<PropertyMetadata<INameLength, TProp, TObj>>>>(
            type_name,
            std::tuple_cat(property_metadata, std::make_tuple(prop)));
    }

    //template<>
    constexpr StaticRefTypeMetadata with_items() {
        return *this;
    }

    template<typename T, typename ... Ts>
    constexpr auto with_items(T item, Ts... items) 
            -> decltype(with_item(item).with_items(items...)) {
        return with_item(item).with_items(items...);
    }

    constexpr TTypeName get_type_name() { return type_name; }
    constexpr TPropertyMetadata get_property_metadata() { return property_metadata; }

    using JSONAssembler = RefTypeJSONAssembler<StaticRefTypeMetadata>;
    typename JSONAssembler::get_json_t json = JSONAssembler::get_as_json(*this);
};

template<size_t INameLength_Plus1>
static constexpr StaticRefTypeMetadata<static_string<INameLength_Plus1-1>, std::tuple<>> make_ref_type_props(const char (&type_name)[INameLength_Plus1]) {
    return StaticRefTypeMetadata<static_string<INameLength_Plus1-1>, std::tuple<>>(static_string<INameLength_Plus1-1>(type_name), std::tuple<>());
}



template<
    typename T,
    typename TMetadata>
class StaticLocalRefType : public LocalRefType {
public:
    constexpr StaticLocalRefType(TMetadata&& metadata) :
        metadata_(std::forward<TMetadata>(metadata)) {}

    using TRandomAccessMetadata = std::array<RectifiedPropertyMetadata, TMetadata::NProperties>;
    TRandomAccessMetadata random_access_metadata = rectify_all_metadata(metadata_.get_property_metadata(), std::make_index_sequence<TMetadata::NProperties>());
    //auto random_access_metadata = rectify_all_metadata(metadata_.get_property_metadata(), std::make_index_sequence<NProperties>());

    ObjectReference_t dereference(ObjectReference_t* ref, size_t index) const final {
        if (index < random_access_metadata.size()) {
            return ObjectReference_t(
                reinterpret_cast<void*>(reinterpret_cast<intptr_t>(ref->obj_) + std::get<1>(random_access_metadata[index])),
                std::get<0>(random_access_metadata[index])
            );
        } else {
            return ObjectReference_t::nil();
        }
    }

    uint16_t get_hash() const final {
        // TODO: implement
        return 0;
    }

    // @brief Returns a JSON snippet that describes this type
    bool get_as_json(const char ** output, size_t* length) const final {
        if (output) *output = metadata_.json.c_str();
        if (length) *length = metadata_.json.size();
        return true;
    }

private:
    TMetadata metadata_;
};

template<typename T, typename TMetadata>
StaticLocalRefType<T, TMetadata> make_local_ref_type(TMetadata&& metadata) {
    using ret_t = StaticLocalRefType<T, TMetadata>;
    return ret_t(std::forward<TMetadata>(metadata));
};

}

#endif // __FIBRE_LOCAL_REF_TYPE
