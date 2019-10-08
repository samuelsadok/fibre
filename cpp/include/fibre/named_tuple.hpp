#ifndef __FIBRE_NAMED_TUPLE_HPP
#define __FIBRE_NAMED_TUPLE_HPP

#include "decoders.hpp"
#include <tuple>

namespace fibre {

/**
 * @brief A named tuple is something in between a tuple and a dict.
 * 
 * Each element in the tuple has a unique name.
 */
template<typename TNames, typename TTypes>
class NamedTuple;

template<typename ... TNames, typename ... TTypes>
class NamedTuple<std::tuple<TNames...>, std::tuple<TTypes...>> : public std::tuple<TTypes...> {
    static_assert(sizeof...(TNames) == sizeof...(TTypes), "number of names and types must be equal");

public:
    //using std::tuple<TTypes...>::tuple;
    NamedTuple(const std::tuple<TTypes...>& tuple)
        : std::tuple<TTypes...>(tuple) {}
};

#if 0

template<typename TInArgNames, typename TInArgTypes>
struct VerboseNamedTupleDecoderV1;

template<
    typename TInArgNames,
    typename ... TInArgTypes>
struct VerboseNamedTupleDecoderV1<TInArgNames, std::tuple<TInArgTypes...>> : Decoder<NamedTuple<TInArgNames, std::tuple<TInArgTypes...>>> {
public:
    StreamSink::status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        StreamSink::status_t status;
        if (waiting_for_key) {
            if ((status = key_decoder->process_bytes(buffer, length, processed_bytes)) != StreamSink::CLOSED) {
                return status;
            }
            call_id_t* call_id = call_id_decoder->get();
            if (call_id != nullptr) {
                start_or_get_call(*call_id, &call_);
            }
            pos_++;
        }
        if (!waiting_for_key) {
            if ((status = val_decoder->process_bytes(buffer, length, processed_bytes)) != StreamSink::CLOSED) {
                return status;
            }
            offset_ = *offset_decoder->get();
            pos_++;
        }
        return StreamSink::OK;
    }

private:
/*

The next problem:
Currently we assume that the type is known given the name. What if the type changes in the next version?
Either need type annotations or need reference arguments by Uuid or so, which implies the type.

*/
    std::tuple<Decoder<TInArgTypes>*...> val_decoders;
    Decoder<const char[123]>* key_decoder; // TODO: find max key length
    StreamSink* val_decoder;
    bool waiting_for_key = true;
    size_t current_val = 0;
};

#endif

}

#endif // __FIBRE_NAMED_TUPLE_HPP
