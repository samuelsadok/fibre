
#ifndef __FIBRE_DECODERS_HPP
#define __FIBRE_DECODERS_HPP

#include "stream.hpp"
#include "crc.hpp"
#include "cpp_utils.hpp"
#include "logging.hpp"
#include <utility>

DEFINE_LOG_TOPIC(DECODERS);
#define current_log_topic LOG_TOPIC_DECODERS

namespace fibre {

/**
 * @brief Represents a decoder that provides a value of type T once it's finished.
 */
template<typename TVal>
class Decoder : public StreamSink {
public:
    /**
     * @brief Shall return a pointer to the decoded value.
     * This value must remain valid for the rest of this object's lifetime.
     * Shall return null if the value is not yet fully available.
     */
    virtual const TVal* get() = 0;
};

/* Base classes --------------------------------------------------------------*/

/*
// @brief Base class for stream based decoders.
// A stream based decoder is a decoder that processes arbitrary length data blocks.
// TODO: this interface is stupid, fix it.
class StreamDecoder : public StreamSink {
public:
    virtual ~StreamDecoder() {};

    // @brief Returns 0 if no error ocurred, otherwise a non-zero error code.
    // Once process_bytes returned an error, subsequent calls to get_status must return the same error.
    // If the decoder is in an error state, the behavior of get_expected_bytes and process_bytes is undefined.
    virtual int get_status() = 0;

    // @brief Returns the minimum number of bytes that are still needed to complete this decoder.
    // If 0, the decoder is considered complete and any subsequent call to process_bytes must process
    // exactly 0 bytes.
    // process_bytes() must always process all provided bytes unless the decoder expects no more bytes
    // afterwards
    virtual size_t get_expected_bytes() = 0;
};*/

/*// @brief Base class for a decoder that is fed in a block-wise fashion.
// This base class is provided for convenience when implementing certain types of decoders.
// A StreamDecoder can be obtained from a BlockDecoder by using StreamDecoder_from_BlockDecoder.
template<unsigned BLOCKSIZE>
class BlockDecoder {
public:
    typedef std::integral_constant<size_t, BLOCKSIZE> block_size;

    virtual int get_status() = 0;
    virtual size_t get_expected_blocks() = 0;
    virtual int process_block(const uint8_t block[BLOCKSIZE]) = 0;
private:
};

// @brief Base class for a decoder that is fed in a byte-wise fashion
// This base class is provided for convenience when implementing certain types of decoders.
// A StreamDecoder can be obtained from a ByteDecoder by using StreamDecoder_from_ByteDecoder.
class ByteDecoder {
public:
    virtual int get_status() = 0;
    virtual size_t get_expected_bytes() = 0;
    virtual int process_byte(uint8_t byte) = 0;
};*/

/* Converter classes ---------------------------------------------------------*/

/*// @brief Encapsulates a BlockDecoder to make it look like a StreamDecoder
// @tparam T The encapsulated BlockDecoder type.
//           Must inherit from BlockDecoder.
template<typename T, ENABLE_IF(TypeChecker<T>::template all_are<BlockDecoder<T::block_size::value>>())>
class StreamDecoder_from_BlockDecoder : public StreamDecoder {
public:
    // @brief Imitates the constructor signature of the encapsulated type.
    template<typename ... Args, ENABLE_IF(TypeChecker<Args...>::template first_is_not<StreamDecoder_from_BlockDecoder>())>
    explicit StreamDecoder_from_BlockDecoder(Args&& ... args)
        : block_decoder_(std::forward<Args>(args)...) {
        EXPECT_TYPE(T, BlockDecoder<T::block_size::value>);
    }

    inline int get_status() final {
        return block_decoder_.get_status();
    }

    inline size_t get_expected_bytes() final {
        size_t expected_bytes = block_decoder_.get_expected_blocks() * T::block_size::value;
        return expected_bytes - std::min(expected_bytes, buffer_pos_);
    }

    inline int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        while (!get_status() && get_expected_bytes() && length) {
            // use the incoming bytes to fill internal buffer to get a complete block
            size_t n_copy = std::min(length, T::block_size::value - buffer_pos_);
            memcpy(buffer_ + buffer_pos_, buffer, n_copy);
            buffer += n_copy;
            length -= n_copy;
            if (processed_bytes) (*processed_bytes) += n_copy;
            buffer_pos_ += n_copy;

            // if we have a full block, process it
            if (buffer_pos_ == T::block_size::value) {
                block_decoder_.process_block(buffer_);
                buffer_pos_ = 0;
            }
        }
        return get_status();
    }

    size_t get_free_space() { return SIZE_MAX; } // TODO: deprecate
private:
    T block_decoder_;
    size_t buffer_pos_ = 0;
    uint8_t buffer_[T::block_size::value];
};

// @brief Encapsulates a ByteDecoder to make it look like a BlockDecoder
// @tparam T The encapsulated ByteDecoder type.
//           Must inherit from ByteDecoder.
template<typename T, ENABLE_IF(TypeChecker<T>::template all_are<ByteDecoder>())>
class BlockDecoder_from_ByteDecoder : public BlockDecoder<1> {
public:
    // @brief Imitates the constructor signature of the encapsulated type.
    template<typename ... Args, ENABLE_IF(TypeChecker<Args...>::template first_is_not<BlockDecoder_from_ByteDecoder>())>
    BlockDecoder_from_ByteDecoder(Args&& ... args)
        : byte_decoder_(std::forward<Args>(args)...) {
        EXPECT_TYPE(T, ByteDecoder);
    }

    inline int get_status() final {
        return byte_decoder_.get_status();
    }
    inline size_t get_expected_blocks() final {
        return byte_decoder_.get_expected_bytes();
    }
    inline int process_block(const uint8_t block[1]) final {
        int status = byte_decoder_.process_byte(*block);
        return status;
    }
private:
    T byte_decoder_;
};

// @brief Encapsulates a ByteDecoder to make it look like a StreamDecoder
// @tparam T The encapsulated ByteDecoder type.
//           Must inherit from ByteDecoder.
template<typename T, ENABLE_IF(TypeChecker<T>::template all_are<ByteDecoder>())>
class StreamDecoder_from_ByteDecoder : public StreamDecoder {
public:
    // @brief Imitates the constructor signature of the encapsulated type.
    template<typename ... Args, ENABLE_IF(TypeChecker<Args...>::template first_is_not<StreamDecoder_from_ByteDecoder>())>
    StreamDecoder_from_ByteDecoder(Args&& ... args)
        : byte_decoder_(std::forward<Args>(args)...) {
        EXPECT_TYPE(T, ByteDecoder);
    }

    inline int get_status() final {
        return byte_decoder_.get_status();
    }
    inline size_t get_expected_bytes() final {
        return byte_decoder_.get_expected_bytes();
    }
    inline size_t get_free_space() { return SIZE_MAX; }
    inline int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) final {
        while (!byte_decoder_.get_status() && byte_decoder_.get_expected_bytes() && length) {
            length--;
            if (processed_bytes) (*processed_bytes)++;
            byte_decoder_.process_byte(*(buffer++));
        }
        return byte_decoder_.get_status();
    }
//private:
    T byte_decoder_;
};*/

/* Decoder implementations ---------------------------------------------------*/

/**
 * @brief Decodes a varint.
 * TODO: don't fail on overflow if desired
 */
template<typename T>
class VarintDecoder : public Decoder<T> {
public:
    static constexpr T BIT_WIDTH = (CHAR_BIT * sizeof(T));

    StreamSink::status_t process_bytes(cbufptr_t& buffer) final {
        while (buffer.length && !is_closed_) {
            uint8_t input_byte = *buffer;
            state_variable_ |= (static_cast<T>(input_byte & 0x7f) << bit_pos_);
            if (((state_variable_ >> bit_pos_) & 0x7f) != static_cast<T>(input_byte & 0x7f)) {
                FIBRE_LOG(E) << "varint overflow: tried to add " << as_hex(input_byte) << " << " << bit_pos_;
                bit_pos_ = BIT_WIDTH;
                return StreamSink::ERROR; // overflow
            }

            buffer++;

            bit_pos_ += 7;
            if (!(input_byte & 0x80))
                is_closed_ = true;
            else if (bit_pos_ >= BIT_WIDTH)
                return StreamSink::ERROR;
        }
        return is_closed_ ? StreamSink::CLOSED : StreamSink::OK;
    }

    const T* get() final {
        return is_closed_ ? &state_variable_ : nullptr;
    }

private:
    T state_variable_ = 0;
    // TODO: adapt size of this variable to hold BIT_WIDTH+6
    size_t bit_pos_ = 0; // bit position
    bool is_closed_ = false;
};


template<typename T, bool BigEndian>
class FixedIntDecoder : public Decoder<T> {
public:
    StreamSink::status_t process_bytes(cbufptr_t& buffer) final {
        size_t chunk = std::min(serializer::BYTE_WIDTH - pos_, buffer.length);
        memcpy(buffer_ + pos_, buffer.ptr, chunk);
        buffer += chunk;
        pos_ += chunk;
        if (pos_ >= serializer::BYTE_WIDTH) {
            value_ = serializer::read(buffer_);
            return StreamSink::CLOSED;
        } else {
            return StreamSink::OK;
        }
    }

    size_t get_min_useful_bytes() const final {
        return serializer::BYTE_WIDTH - pos_;
    }

    size_t get_min_non_blocking_bytes() const final {
        return serializer::BYTE_WIDTH - pos_;
    }

    const T* get() const { return (pos_ >= serializer::BYTE_WIDTH) ? &value_ : nullptr; }
    const T* get() { return (pos_ >= serializer::BYTE_WIDTH) ? &value_ : nullptr; }

private:
    using serializer = SimpleSerializer<T, BigEndian>;
    uint8_t buffer_[serializer::BYTE_WIDTH];
    size_t pos_ = 0;
    T value_ = 0;
};

/**
 * @brief Decodes a UTF-8 encoded string into a local representation of the
 * string.
 * 
 * The characters will be saved into a fixed size array of a given data type,
 * along with a variable that indicates the length of the string.
 * 
 * Characters that are too large for type T will be substituted with either
 * 0xFFFD if T is at least 16 bits or 0x3f ('?') otherwise. TODO: this is not fully implemented
 * 
 * TODO: define error handling: should we store an invalid character and proceed or should we cancel?
 * given that the length becomes undefined then probably we should fail.
 */
template<typename T, size_t MAX_SIZE>
class UTF8Decoder : public Decoder<std::tuple<std::array<T, MAX_SIZE>, size_t>> {
public:
    static constexpr T replacement_char = sizeof(T) * CHAR_BIT >= 16 ? 0xfffd : 0x3f;

    StreamSink::status_t process_bytes(cbufptr_t& buffer) final {
        StreamSink::status_t status;
        std::array<T, MAX_SIZE>& received_buf = std::get<0>(value_);
        size_t& received_length = std::get<1>(value_);

        if (!length_decoder_.get()) {
            if ((status = length_decoder_.process_bytes(buffer)) != StreamSink::CLOSED) {
                return status;
            }
            FIBRE_LOG(D) << "UTF-8: received length " << *length_decoder_.get();
        }
        if (length_decoder_.get()) {
            while (received_length < *length_decoder_.get()) {
                if (!buffer.length) {
                    return StreamSink::OK;
                }

                uint8_t byte = *(buffer++);
                if (byte & 0xc0 == 0x80) {
                    if (received_length == 0) {
                        FIBRE_LOG(W) << "UTF-8 continuation byte in beginning";
                    } else {
                        // TODO: detect bit overflow (too large code points)
                        received_buf[received_length - 1] <<= 6;
                        received_buf[received_length - 1] += byte & 0x3f;
                    }
                } else {
                    if ((byte & 0x80) == 0x00) {
                        byte &= 0x7f;
                    } else if ((byte & 0xe0) == 0xc0) {
                        byte &= 0x1f;
                    } else if ((byte & 0xf0) == 0xe0) {
                        byte &= 0x0f;
                    } else if ((byte & 0xf8) == 0xf0) {
                        byte &= 0x07;
                    } else {
                        FIBRE_LOG(W) << "unexpected UTF-8 sequence " << as_hex(byte);
                        byte = replacement_char;
                    }
                    if (byte > std::numeric_limits<T>::max()) { // very improbable
                        byte = replacement_char;
                    }
                    received_buf[received_length++] = byte;
                }
            }
        }
        return StreamSink::CLOSED;
    }

    const std::tuple<std::array<T, MAX_SIZE>, size_t>* get() final {
        if (length_decoder_.get() && std::get<1>(value_) == *length_decoder_.get()) {
            return &value_;
        } else {
            return nullptr;
        }
    }

private:
    VarintDecoder<size_t> length_decoder_;
    std::tuple<std::array<T, MAX_SIZE>, size_t> value_;
};

//template<typename T>
//using VarintStreamDecoder = StreamDecoder_from_ByteDecoder<VarintByteDecoder<T>>;

// This double nested type should work identically but makes it way harder for the compiler to optimize
//template<typename T>
//using VarintBlockDecoder = BlockDecoder_from_ByteDecoder<VarintByteDecoder<T>>;
//template<typename T>
//using VarintStreamDecoder = StreamDecoder_from_BlockDecoder<VarintBlockDecoder<T>>;



/*template<uint8_t INIT, uint8_t POLYNOMIAL, typename TDecoder,
        ENABLE_IF(TypeChecker<TDecoder>::template all_are<StreamSink>())>
class CRC8BlockDecoder : public BlockDecoder<CRC8_BLOCKSIZE> {
public:
    CRC8BlockDecoder(TDecoder&& inner_decoder) :
            inner_decoder_(std::forward<TDecoder>(inner_decoder)) {
    }

    int get_status() final {
        return status_;
    }

    size_t get_expected_blocks() final {
        return (inner_decoder_.get_expected_bytes() + CRC8_BLOCKSIZE - 2) / (CRC8_BLOCKSIZE - 1);
    }

    status_t process_block(const uint8_t input_block[4]) final {
        current_crc_ = calc_crc8<POLYNOMIAL>(current_crc_, input_block, CRC8_BLOCKSIZE - 1);
        if (current_crc_ != input_block[CRC8_BLOCKSIZE - 1])
            return ERROR;
        return inner_decoder_.process_bytes(input_block, CRC8_BLOCKSIZE - 1, nullptr);
    }
private:
    TDecoder inner_decoder_;
    int status_ = 0;
    uint8_t current_crc_ = INIT;
};

template<unsigned INIT, unsigned POLYNOMIAL, typename TDecoder>
using CRC8StreamDecoder = StreamDecoder_from_BlockDecoder<CRC8BlockDecoder<INIT, POLYNOMIAL, TDecoder>>;

template<unsigned INIT, unsigned POLYNOMIAL, typename TDecoder>
inline CRC8StreamDecoder<INIT, POLYNOMIAL, TDecoder> make_crc8_decoder(TDecoder&& decoder) {
    return CRC8StreamDecoder<INIT, POLYNOMIAL, TDecoder>(std::forward<TDecoder>(decoder));
}*/

/*// TODO: ENABLE_IF(TypeChecker<TDecoders...>::template all_are<StreamDecoder>())
template<typename ... TDecoders>
class DecoderChain;

template<>
class DecoderChain<> : public StreamDecoder {
public:
    size_t get_expected_bytes() { return 0; }
    int get_status() { return 0; }
    int process_bytes(const uint8_t *input, size_t length, size_t* processed_bytes) { return 0; }
    size_t get_free_space() { return SIZE_MAX; } // TODO: deprecate
};

template<typename TDecoder, typename ... TDecoders>
class DecoderChain<TDecoder, TDecoders...> : public StreamDecoder {
public:
    DecoderChain(TDecoder&& this_decoder, TDecoders&& ... subsequent_decoders) :
        this_decoder_(std::forward<TDecoder>(this_decoder)),
        subsequent_decoders_(std::forward<TDecoders>(subsequent_decoders)...)
    {
        EXPECT_TYPE(TDecoder, StreamDecoder);
    }

    int get_status() final {
        // If this decoder or any of the subsequent decoders failed, return error code.
        int this_status = this_decoder_.get_status();
        int subsequent_status = subsequent_decoders_.get_status();
        if (this_status)
            return this_status;
        else if (subsequent_status)
            return subsequent_status;
        else
            return 0;
    }

    size_t get_expected_bytes() final {
        return this_decoder_.get_expected_bytes() + subsequent_decoders_.get_expected_bytes();
    }

    int process_bytes(const uint8_t *input, size_t length, size_t* processed_bytes) final {
        if (this_decoder_.get_expected_bytes()) {
            LOG_FIBRE("decoder chain: process %zu bytes in segment %s\n", length, typeid(TDecoder).name());
            size_t chunk = 0;
            int status = this_decoder_.process_bytes(input, length, &chunk);
            input += chunk;
            length -= chunk;
            if (processed_bytes) (*processed_bytes) += chunk;
            if (status)
                return status;
            if (!length)
                return 0;
        }
        return subsequent_decoders_.process_bytes(input, length, processed_bytes);
    }
    
    size_t get_free_space() { return SIZE_MAX; } // TODO: deprecate
private:
    TDecoder this_decoder_;
    DecoderChain<TDecoders...> subsequent_decoders_;
};*/


}

#undef current_log_topic

#endif // __FIBRE_DECODERS_HPP
