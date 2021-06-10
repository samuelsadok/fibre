#ifndef __FIBRE_CODECS_HPP
#define __FIBRE_CODECS_HPP

#include "static_exports.hpp"
#include <fibre/simple_serdes.hpp>
#include <fibre/bufptr.hpp>
#include <fibre/domain.hpp>
#include <fibre/rich_status.hpp>
#include <optional>
#include "property.hpp"
#include <fibre/fibre.hpp>

namespace fibre {

template<typename T, typename = void>
struct Codec {
    static RichStatusOr<T> decode(Domain* domain, cbufptr_t* buffer) {
        F_LOG_E(domain->ctx->logger, "unknown decoder for " << typeid(T).name());
        return std::nullopt; }
};

template<> struct Codec<bool> {
    static RichStatusOr<bool> decode(Domain* domain, cbufptr_t* buffer) { return (buffer->begin() == buffer->end()) ? RichStatusOr<bool>{F_MAKE_ERR("empty buffer")} : RichStatusOr<bool>((bool)*(buffer->begin()++)); }
    static bool encode(bool value, bufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};

template<typename T>
struct Codec<T, std::enable_if_t<std::is_integral<T>::value>> {
    static RichStatusOr<T> decode(Domain* domain, cbufptr_t* buffer) {
        std::optional<T> val = SimpleSerializer<T, false>::read(&(buffer->begin()), buffer->end());
        if (val.has_value()) {
            return *val;
        } else {
            return F_MAKE_ERR("decode failed");
        }
    }
    static bool encode(T value, bufptr_t* buffer) { return SimpleSerializer<T, false>::write(value, &(buffer->begin()), buffer->end()); }
};

template<> struct Codec<float> {
    static RichStatusOr<float> decode(Domain* domain, cbufptr_t* buffer) {
        RichStatusOr<uint32_t> int_val = Codec<uint32_t>::decode(domain, buffer);
        return int_val.has_value() ? RichStatusOr<float>(*reinterpret_cast<float*>(&int_val.value())) : int_val.status();
    }
    static bool encode(float value, bufptr_t* buffer) {
        void* ptr = &value;
        return Codec<uint32_t>::encode(*reinterpret_cast<uint32_t*>(ptr), buffer);
    }
};
template<typename T>
struct Codec<T, std::enable_if_t<std::is_enum<T>::value>> {
    using int_type = std::underlying_type_t<T>;
    static std::optional<T> decode(Domain* domain, cbufptr_t* buffer) {
        std::optional<int_type> int_val = SimpleSerializer<int_type, false>::read(&(buffer->begin()), buffer->end());
        return int_val.has_value() ? std::make_optional(static_cast<T>(*int_val)) : std::nullopt;
    }
    static bool encode(T value, bufptr_t* buffer) { return SimpleSerializer<int_type, false>::write(value, &(buffer->begin()), buffer->end()); }
};


template<typename T> struct Codec<T*> {
    static RichStatusOr<T*> decode(Domain* domain, cbufptr_t* buffer) {
#if FIBRE_ENABLE_SERVER
        uint8_t idx = (*buffer)[0]; // TODO: define actual decoder

        // Check object type
        ServerObjectDefinition* obj_entry = domain->get_server_object(idx);

        F_RET_IF(!obj_entry,
                 "index out of range");

        F_RET_IF(obj_entry->interface != get_interface_id<T>(),
                 "incompatile interface: expected " << (int)obj_entry->interface << " but got " << (int)get_interface_id<T>());

        *buffer = buffer->skip(1);
        return (T*)obj_entry->ptr;
#else
        return  F_MAKE_ERR("no server support compiled in");
#endif
    }

    static bool encode(bool value, bufptr_t* buffer) { return false; }
};

}

#endif // __FIBRE_CODECS_HPP
