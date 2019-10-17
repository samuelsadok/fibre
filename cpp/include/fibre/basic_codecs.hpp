#ifndef __FIBRE_BASIC_CODECS_HPP
#define __FIBRE_BASIC_CODECS_HPP

#include "decoder.hpp"
#include "encoder.hpp"

#include <limits.h>

DEFINE_LOG_TOPIC(BASIC_CODECS);
#define current_log_topic LOG_TOPIC_BASIC_CODECS

namespace fibre {

/* Integers ------------------------------------------------------------------*/

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

template<typename T>
class VarintEncoder : public Encoder<T> {
public:
    static constexpr T BIT_WIDTH = (CHAR_BIT * sizeof(T));

    VarintEncoder()
    {}

    void set(T* value) {
        value_ = value;
    }

    StreamSource::status_t get_bytes(bufptr_t& buffer) final {
        while (value_ && buffer.length) {
            if (bit_pos_ == 0)
                FIBRE_LOG(D) << "start encoding varint, from pos " << bit_pos_;
            *buffer = (*value_ >> bit_pos_) & 0x7f;
            bit_pos_ += 7;
            if (bit_pos_ < BIT_WIDTH && (*value_ >> bit_pos_)) {
                FIBRE_LOG(D) << "remainder: " << as_hex(*value_ >> bit_pos_);
                *buffer |= 0x80;
            } else {
                value_ = nullptr;
            }
            buffer++;
        }
        return value_ ? StreamSource::OK : StreamSource::CLOSED;
    }

private:
    T* value_ = nullptr;
    size_t bit_pos_ = 0; // bit position
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

template<typename T, bool BigEndian>
class FixedIntEncoder : public Encoder<T> {
public:
    void set(T* value_) {
        if (value_) {
            serializer::write(*value_, buffer_);
            pos_ = 0;
        } else {
            pos_ = SIZE_MAX;
        }
    }

    StreamSource::status_t get_bytes(bufptr_t& buffer) final {
        size_t chunk = std::min(serializer::BYTE_WIDTH - pos_, buffer.length);
        memcpy(buffer.ptr, buffer_ + pos_, chunk);
        buffer += chunk;
        pos_ += chunk;
        if (pos_ >= serializer::BYTE_WIDTH) {
            return StreamSource::CLOSED;
        } else {
            return StreamSource::OK;
        }
    }

private:
    using serializer = SimpleSerializer<T, BigEndian>;
    uint8_t buffer_[serializer::BYTE_WIDTH];
    size_t pos_ = SIZE_MAX;
};

/* Strings -------------------------------------------------------------------*/

/**
 * @brief Decodes a UTF-8 encoded string into a local representation of the
 * string.
 * 
 * Implementations for various local string types exist.
 * 
 * Characters that are too large for type T will be substituted with either
 * 0xFFFD if T is at least 16 bits or 0x3f ('?') otherwise. TODO: this is not fully implemented
 * 
 * TODO: define error handling: should we store an invalid character and proceed or should we cancel?
 * given that the length becomes undefined then probably we should fail.
 */
template<typename TStr>
class UTF8Decoder;

/**
 * @brief Decodes a UTF-8 encoded string into a local representation of the
 * string.
 * 
 * The characters will be saved into a fixed size array of a given data type,
 * along with a variable that indicates the length of the string.
 */
template<typename T, size_t MAX_SIZE>
class UTF8Decoder<std::tuple<std::array<T, MAX_SIZE>, size_t>> : public Decoder<std::tuple<std::array<T, MAX_SIZE>, size_t>> {
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

template<char... CHARS>
class UTF8Decoder<sstring<CHARS...>> : public Decoder<sstring<CHARS...>> {
public:
    StreamSink::status_t process_bytes(cbufptr_t& buffer) final {
        StreamSink::status_t status;
        if ((status = impl_.process_bytes(buffer)) != StreamSink::CLOSED) {
            return status;
        }
        return get() ? StreamSink::CLOSED : StreamSink::ERROR;
    }

    sstring<CHARS...>* get() final {
        auto received_value = impl_.get();
        if (received_value) {
            bool matches = (std::get<1>(*received_value) == sizeof...(CHARS))
                    && (std::get<0>(*received_value) == value_.as_array());
            return matches ? &value_ : nullptr;
        } else {
            return nullptr;
        }
    }

private:
    sstring<CHARS...> value_{};
    UTF8Decoder<std::tuple<std::array<char, sizeof...(CHARS)>, size_t>> impl_;
};

/**
 * @brief Encodes a local representation of a string using the UTF-8 scheme.
 * 
 * Implementations for various local string types exist.
 * 
 * Characters that generate a sequence longer than 4 bytes will be substituted
 * with the 
 * 0xFFFD if T is at least 16 bits or 0x3f ('?') otherwise. TODO: this is not fully implemented
 * 
 * TODO: define error handling: should we emit an invalid character and proceed or should we cancel?
 */
template<typename TStr>
class UTF8Encoder;

/**
 * @brief Encodes a local representation of a string using the UTF-8 scheme.
 * 
 * The characters are taken from a fixed size array of a given data type,
 * along with a variable that indicates the length of the string.
 */
template<typename T, size_t MAX_SIZE>
class UTF8Encoder<std::tuple<std::array<T, MAX_SIZE>, size_t>> : public Encoder<std::tuple<std::array<T, MAX_SIZE>, size_t>> {
public:
    static constexpr T replacement_char = sizeof(T) * CHAR_BIT >= 16 ? 0xfffd : 0x3f;

    void set(std::tuple<std::array<T, MAX_SIZE>, size_t>* value) {
        length_encoder_.set(value ? &(std::get<1>(*value)) : nullptr);
        value_ = value;
        sent_length_ = 0;
        tmp_buf_len_ = 0;
    }

    StreamSource::status_t get_bytes(bufptr_t& buffer) final {
        if (!value_)
            return StreamSource::CLOSED;

        StreamSource::status_t status;
        std::array<T, MAX_SIZE>& str_buf = std::get<0>(*value_);
        size_t& str_length = std::get<1>(*value_);

        if ((status = length_encoder_.get_bytes(buffer)) != StreamSource::CLOSED) {
            return status;
        }

        while (buffer.length && (tmp_buf_len_ || (sent_length_ < str_length))) {
            if (!tmp_buf_len_) {
                T chr = str_buf[sent_length_++];
                if (chr < 0x80) {
                    tmp_buf_[3] = chr;
                    tmp_buf_len_ = 1;
                } else if (chr < 0x800) {
                    tmp_buf_[2] = 0xc0 | ((chr >> 6) & 0x1f);
                    tmp_buf_[3] = 0x80 | ((chr >> 0) & 0x3f);
                    tmp_buf_len_ = 2;
                } else if (chr < 0x10000) {
                    tmp_buf_[1] = 0xe0 | ((chr >> 12) & 0x0f);
                    tmp_buf_[2] = 0x80 | ((chr >> 6) & 0x3f);
                    tmp_buf_[3] = 0x80 | ((chr >> 0) & 0x3f);
                    tmp_buf_len_ = 3;
                } else if (chr < 0x110000) {
                    tmp_buf_[0] = 0xf0 | ((chr >> 18) & 0x07);
                    tmp_buf_[1] = 0x80 | ((chr >> 12) & 0x3f);
                    tmp_buf_[2] = 0x80 | ((chr >> 6) & 0x3f);
                    tmp_buf_[3] = 0x80 | ((chr >> 0) & 0x3f);
                    tmp_buf_len_ = 4;
                } else {
                    // insert replacement character
                    tmp_buf_[1] = 0xef;
                    tmp_buf_[2] = 0xbf;
                    tmp_buf_[3] = 0xbd;
                    tmp_buf_len_ = 3;
                }
            }

            *(buffer++) = tmp_buf_[4 - (tmp_buf_len_--)];
        }
        
        return (tmp_buf_len_ || sent_length_ < str_length) ? StreamSource::OK : StreamSource::CLOSED;
    }

private:
    VarintEncoder<size_t> length_encoder_;
    std::tuple<std::array<T, MAX_SIZE>, size_t>* value_;
    uint8_t tmp_buf_[4]; // max 4 bytes per sequence
    size_t tmp_buf_len_ = 0;
    size_t sent_length_ = 0;
};

template<char... CHARS>
class UTF8Encoder<sstring<CHARS...>> : public Encoder<sstring<CHARS...>> {
public:
    void set(sstring<CHARS...>* val) final {
        impl_.set(val ? &value_ : nullptr);
    }

    StreamSource::status_t get_bytes(bufptr_t& buffer) final {
        return impl_.get_bytes(buffer);
    }

private:
    std::tuple<std::array<char, sizeof...(CHARS)>, size_t> value_ = {{CHARS...}, sizeof...(CHARS)};
    UTF8Encoder<std::tuple<std::array<char, sizeof...(CHARS)>, size_t>> impl_;
};

}

#undef current_log_topic

#endif // __FIBRE_BASIC_CODECS_HPP