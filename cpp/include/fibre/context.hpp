#ifndef __FIBRE_CONTEXT_HPP
#define __FIBRE_CONTEXT_HPP

#include "stream.hpp"
#include "crc.hpp"
#include "cpp_utils.hpp"
#include "logging.hpp"
#include <utility>
#include <memory>

// need to include all default encoders
#include "basic_codecs.hpp"
#include "uuid_codecs.hpp"

DEFINE_LOG_TOPIC(CONTEXT);

namespace fibre {

template<typename T>
struct default_codec;

template<typename T>
using default_decoder_t = typename default_codec<T>::dec_type;

template<typename T>
using default_encoder_t = typename default_codec<T>::enc_type;

template<>
struct default_codec<unsigned int> {
    using dec_type = VarintDecoder<unsigned int>;
    using enc_type = VarintEncoder<unsigned int>;
};

template<>
struct default_codec<long unsigned int> {
    using dec_type = VarintDecoder<long unsigned int>;
    using enc_type = VarintEncoder<long unsigned int>;
};

template<size_t MAX_SIZE>
struct default_codec<std::tuple<std::array<char, MAX_SIZE>, size_t>> {
    using dec_type = UTF8Decoder<std::tuple<std::array<char, MAX_SIZE>, size_t>>;
    using enc_type = UTF8Encoder<std::tuple<std::array<char, MAX_SIZE>, size_t>>;
};

template<>
struct default_codec<Uuid> {
    using dec_type = BigEndianUuidDecoder;
    using enc_type = BigEndianUuidEncoder;
};

template<char... CHARS>
struct default_codec<sstring<CHARS...>> {
    //using dec_type = UTF8Decoder<sstring<CHARS...>>;
    using enc_type = UTF8Encoder<sstring<CHARS...>>;
};

struct Context {
    std::shared_ptr<StreamSink> preferred_tx_channel;
    void add_tx_channel(std::shared_ptr<StreamSink> sink) {
        preferred_tx_channel = sink;
    }
};

/**
 * @brief Decodes T based on the context it is executed on.
 * 
 * Each supported type has a default decoder but there are ways to change the
 * decoder of a type for a particular context.
 */
template<typename T>
Decoder<T>* alloc_decoder(Context* ctx) {
    return new default_decoder_t<T>; // TODO: remove dynamic allocation
}

template<typename T>
void dealloc_decoder(Decoder<T>* decoder) {
    delete decoder;
}


/**
 * @brief Encodes T based on the context it is executed on.
 * 
 * Each supported type has a default encoder but there are ways to change the
 * encoder of a type for a particular context.
 */
template<typename T>
Encoder<T>* alloc_encoder(Context* ctx) {
    return new default_encoder_t<T>; // TODO: remove dynamic allocation
}

template<typename T>
void dealloc_encoder(Encoder<T>* decoder) {
    delete decoder;
}


}

#endif // __FIBRE_CONTEXT_HPP
