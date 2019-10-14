#ifndef __FIBRE_NAMED_TUPLE_HPP
#define __FIBRE_NAMED_TUPLE_HPP

#include "decoder.hpp"
#include "print_utils.hpp"
#include <tuple>

DEFINE_LOG_TOPIC(NAMED_TUPLE);
#define current_log_topic LOG_TOPIC_NAMED_TUPLE

namespace fibre {

#if 0
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
#endif


template<typename TNames, typename TTypes>
struct VerboseNamedTupleDecoderV1;
template<typename TNames, typename TTypes>
struct VerboseNamedTupleEncoderV1;

}
#include "context.hpp"
namespace fibre {

/*
WARNING: this is very ugly code. But hey, it passes the unit tests.
TODO: make less ugly
*/
template<
    typename TNames,
    typename ... TTypes>
struct VerboseNamedTupleDecoderV1<TNames, std::tuple<TTypes...>> : Decoder<std::tuple<TTypes...>> {
public:
    VerboseNamedTupleDecoderV1(Context* ctx, TNames names, std::tuple<TTypes...> default_values) // TODO: add optional bool array
        : ctx_(ctx), names_(names), values_(default_values), key_decoder_(alloc_decoder<TDecodedKey>(ctx_)) {}

    StreamSink::status_t process_bytes(cbufptr_t& buffer) final {
        StreamSink::status_t status;
        while (buffer.length && (received_vals_ < sizeof...(TTypes))) {
            if (waiting_for_key_) {
                FIBRE_LOG(D) << "process key byte " << (buffer.length ? as_hex(*buffer) : as_hex((uint8_t)0));
                if ((status = key_decoder_->process_bytes(buffer)) != StreamSink::CLOSED) {
                    return status;
                }
                FIBRE_LOG(D) << "received key: " << *key_decoder_->get();
                val_decoder_ = init_matching(std::make_index_sequence<sizeof...(TTypes)>());
                if (!val_decoder_) {
                    FIBRE_LOG(D) << "received unknown key: " << *key_decoder_->get();
                    return StreamSink::ERROR;
                }
                waiting_for_key_ = false;
            }
            if (!waiting_for_key_) {
                FIBRE_LOG(D) << "process val byte " << (buffer.length ? as_hex(*buffer) : as_hex((uint8_t)0));
                if ((status = val_decoder_->process_bytes(buffer)) != StreamSink::CLOSED) {
                    return status;
                }
                deinit_matching(std::make_index_sequence<sizeof...(TTypes)>());
                dealloc_decoder<TDecodedKey>(key_decoder_);
                key_decoder_ = alloc_decoder<TDecodedKey>(ctx_); // TODO: dealloc
                waiting_for_key_ = true;
                FIBRE_LOG(D) << "received val number " << received_vals_;
            }
        }
        return (received_vals_ >= sizeof...(TTypes)) ? StreamSink::CLOSED : StreamSink::OK;
    }

    std::tuple<TTypes...>* get() final {
        return (received_vals_ >= sizeof...(TTypes)) ? &values_ : nullptr;
    }

private:

    template<size_t ... Is>
    StreamSink* init_matching(std::index_sequence<Is...>) {
        StreamSink* decoders[] = {init_if_match<Is>()...};
        for (size_t i = 0; i < sizeof...(Is); ++i) {
            if (decoders[i]) {
                return decoders[i];
            }
        }
        return nullptr;
    }

    template<size_t I>
    StreamSink* init_if_match() {
        auto recv_key_buf = std::get<0>(*key_decoder_->get()).data();
        auto recv_key_len = std::get<1>(*key_decoder_->get());
        auto key = std::get<I>(names_);
        using T = std::tuple_element_t<I, std::tuple<TTypes...>>;
        if ((key.size() == recv_key_len) && (memcmp(key.c_str(), recv_key_buf, recv_key_len) == 0)) {
            return std::get<I>(val_decoders_) = alloc_decoder<T>(ctx_);
        }
        FIBRE_LOG(D) << "key "  << key.c_str() << " does not match";
        return nullptr;
    }

    template<size_t ... Is>
    void deinit_matching(std::index_sequence<Is...>) {
        int decoders[] = {(deinit_if_match<Is>(), 0)...};
    }

    template<size_t I>
    void deinit_if_match() {
        auto recv_key_buf = std::get<0>(*key_decoder_->get()).data();
        auto recv_key_len = std::get<1>(*key_decoder_->get());
        auto key = std::get<I>(names_);
        using T = std::tuple_element_t<I, std::tuple<TTypes...>>;
        if ((key.size() == recv_key_len) && (memcmp(key.c_str(), recv_key_buf, recv_key_len) == 0)) {
            std::get<I>(values_) = *std::get<I>(val_decoders_)->get();
            dealloc_decoder<T>(std::get<I>(val_decoders_));
            std::get<I>(val_decoders_) = nullptr;
            received_vals_++;
        }
    }


    using TDecodedKey = std::tuple<std::array<char, 128>, size_t>;
    Context* ctx_;
    TNames names_;
    std::tuple<TTypes...> values_;
    std::tuple<Decoder<TTypes>*...> val_decoders_;
    Decoder<TDecodedKey>* key_decoder_; // TODO: find max key length
    StreamSink* val_decoder_ = nullptr;
    bool waiting_for_key_ = true;
    size_t received_vals_ = 0;
};


/*
WARNING: this is very ugly code. But hey, it passes the unit tests.
TODO: make less ugly
*/
template<
    typename TNames,
    typename ... TTypes>
struct VerboseNamedTupleEncoderV1<TNames, std::tuple<TTypes...>> : Encoder<std::tuple<TTypes...>> {
public:
    VerboseNamedTupleEncoderV1(Context* ctx, TNames names) // TODO: add optional bool array
        : ctx_(ctx), names_(names) {}
    
    void set(std::tuple<TTypes...>* values) final {
        values_ = values;
        pos_ = 0;
        set_next();
    }

protected:
    void set_next() {
        if (!values_) {
            current_stream_ = nullptr;
        } else {
            current_stream_ = init(pos_ & 1, pos_ >> 1, std::make_index_sequence<sizeof...(TTypes)>());
            if (current_stream_) {
                pos_++;
            }
        }
    }

private:
    template<size_t ... Is>
    StreamSource* init(bool init_val_encoder, size_t num, std::index_sequence<Is...>) {
        StreamSource* streams[] =
            {((num == Is) ? init_ith_stream<Is>(init_val_encoder) : nullptr)...};
        return (num < sizeof...(TTypes)) ? streams[num] : nullptr;
    }

    template<size_t I>
    StreamSource* init_ith_stream(bool init_val_encoder) {
        using T = std::tuple_element_t<I, std::tuple<TTypes...>>;
        if (!init_val_encoder) {
            Encoder<std::tuple_element_t<I, TNames>>* key_encoder =
                alloc_encoder<std::tuple_element_t<I, TNames>>(ctx_);
            key_encoder->set(&(std::get<I>(names_)));
            FIBRE_LOG(D) << "encode key #" << I;
            return key_encoder;
        } else {
            Encoder<T>* val_encoder =
                alloc_encoder<T>(ctx_);
            val_encoder->set(&(std::get<I>(*values_)));
            FIBRE_LOG(D) << "encode val #" << I;
            return val_encoder;
        }
    }

    StreamSource::status_t get_bytes(bufptr_t& buffer) final {
        StreamSource::status_t status;
        while (buffer.length && current_stream_) {
            if ((status = current_stream_->get_bytes(buffer)) != StreamSource::CLOSED) {
                return status;
            }
            set_next();
        }
        return current_stream_ ? StreamSource::OK : StreamSource::CLOSED;
    }

private:
    TNames names_;
    std::tuple<TTypes...>* values_;
    Context* ctx_;
    StreamSource* current_stream_ = nullptr;
    size_t pos_ = 0;
};




}

#undef current_log_topic

#endif // __FIBRE_NAMED_TUPLE_HPP
