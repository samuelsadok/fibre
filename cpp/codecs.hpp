#ifndef __FIBRE_CODECS_HPP
#define __FIBRE_CODECS_HPP

#include "logging.hpp"
#include "static_exports.hpp"
#include <fibre/simple_serdes.hpp>
#include <fibre/bufptr.hpp>
#include <optional>
#include "property.hpp"

DEFINE_LOG_TOPIC(CODEC);
#define current_log_topic LOG_TOPIC_CODEC

namespace fibre {

template<typename T, typename = void>
struct Codec {
    static std::optional<T> decode(Domain* domain, cbufptr_t* buffer) {
        printf("unknown decoder for %s\n", typeid(T).name());
        return std::nullopt; }
};

template<> struct Codec<bool> {
    static std::optional<bool> decode(Domain* domain, cbufptr_t* buffer) { return (buffer->begin() == buffer->end()) ? std::nullopt : std::make_optional((bool)*(buffer->begin()++)); }
    static bool encode(bool value, bufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int8_t> {
    static std::optional<int8_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<int8_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int8_t value, bufptr_t* buffer) { return SimpleSerializer<int8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint8_t> {
    static std::optional<uint8_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint8_t value, bufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int16_t> {
    static std::optional<int16_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<int16_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int16_t value, bufptr_t* buffer) { return SimpleSerializer<int16_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint16_t> {
    static std::optional<uint16_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<uint16_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint16_t value, bufptr_t* buffer) { return SimpleSerializer<uint16_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int32_t> {
    static std::optional<int32_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<int32_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int32_t value, bufptr_t* buffer) { return SimpleSerializer<int32_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint32_t> {
    static std::optional<uint32_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<uint32_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint32_t value, bufptr_t* buffer) { return SimpleSerializer<uint32_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int64_t> {
    static std::optional<int64_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<int64_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int64_t value, bufptr_t* buffer) { return SimpleSerializer<int64_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint64_t> {
    static std::optional<uint64_t> decode(Domain* domain, cbufptr_t* buffer) { return SimpleSerializer<uint64_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint64_t value, bufptr_t* buffer) { return SimpleSerializer<uint64_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<float> {
    static std::optional<float> decode(Domain* domain, cbufptr_t* buffer) {
        std::optional<uint32_t> int_val = Codec<uint32_t>::decode(domain, buffer);
        return int_val.has_value() ? std::optional<float>(*reinterpret_cast<float*>(&*int_val)) : std::nullopt;
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

//template<> struct Codec<endpoint_ref_t> {
//    static std::optional<endpoint_ref_t> decode(Domain* domain, cbufptr_t* buffer) {
//        std::optional<uint16_t> val0 = SimpleSerializer<uint16_t, false>::read(&(buffer->begin()), buffer->end());
//        std::optional<uint16_t> val1 = SimpleSerializer<uint16_t, false>::read(&(buffer->begin()), buffer->end());
//        return (val0.has_value() && val1.has_value()) ? std::make_optional(endpoint_ref_t{*val1, *val0}) : std::nullopt;
//    }
//    static bool encode(endpoint_ref_t value, bufptr_t* buffer) {
//        return SimpleSerializer<uint16_t, false>::write(value.endpoint_id, &(buffer->begin()), buffer->end())
//            && SimpleSerializer<uint16_t, false>::write(value.json_crc, &(buffer->begin()), buffer->end());
//    }
//};


//, std::enable_if_t<(get_interface_id<T>(), true)
template<typename T> struct Codec<T*> {
    static std::optional<T*> decode(Domain* domain, cbufptr_t* buffer) {
        uint8_t idx = (*buffer)[0]; // TODO: define actual decoder

        // Check object type
        ServerObjectDefinition* obj_entry = domain->get_server_object(idx);

        if (!obj_entry) {
            FIBRE_LOG(W) << "index out of range";
            return std::nullopt;
        }

        if (obj_entry->interface != get_interface_id<T>()) {
            FIBRE_LOG(W) << "incompatile interface: expected " << (int)obj_entry->interface << " but got " << (int)get_interface_id<T>();
            return std::nullopt;
        }

        *buffer = buffer->skip(1);
        return (T*)obj_entry->ptr;
    }

    static bool encode(bool value, bufptr_t* buffer) { return false; }
};

}

#undef current_log_topic

#endif // __FIBRE_CODECS_HPP
