#ifndef __FIBRE_CONTEXT_HPP
#define __FIBRE_CONTEXT_HPP

#include "stream.hpp"
#include "crc.hpp"
#include "cpp_utils.hpp"
#include "logging.hpp"
#include <utility>

// need to include all default encoders
#include "decoders.hpp"
#include "named_tuple.hpp"

DEFINE_LOG_TOPIC(CONTEXT);

namespace fibre {

template<typename T>
struct default_decoder;

template<typename T>
using default_decoder_t = typename default_decoder<T>::type;

template<typename TInArgNames, typename TInArgTypes>
struct default_decoder<NamedTuple<TInArgNames, TInArgTypes>> { /*using type = VerboseNamedTupleDecoderV1<TInArgNames, TInArgTypes>;*/ };


struct Context;

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


}

#endif // __FIBRE_CONTEXT_HPP
