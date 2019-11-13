#ifndef __FIBRE_UUID_CODECS_HPP
#define __FIBRE_UUID_CODECS_HPP

#include "decoder.hpp"
#include "encoder.hpp"
#include "uuid.hpp"

#include <limits.h>

DEFINE_LOG_TOPIC(UUID_CODECS);
#define current_log_topic LOG_TOPIC_UUID_CODECS

namespace fibre {

class BigEndianUuidDecoder : public Decoder<Uuid> {
public:
    StreamStatus process_bytes(cbufptr_t& buffer) final {
        size_t chunk = std::min(buffer.length, sizeof(buf_) - pos_);
        memcpy(buf_ + pos_, buffer.ptr, chunk);
        buffer += chunk;
        pos_ += chunk;
        
        if (pos_ >= 16) {
            uuid_ = Uuid{buf_};
            return kStreamClosed;
        } else {
            return kStreamOk;
        }
    }

    Uuid* get() final {
        return pos_ >= 16 ? &uuid_ : nullptr;
    }

private:
    Uuid uuid_;
    uint8_t buf_[16];
    size_t pos_ = 0;
};

class BigEndianUuidEncoder : public Encoder<Uuid> {
public:
    void set(Uuid* value) final {
        value_ = value;
        pos_ = 0;
    }

    StreamStatus get_bytes(bufptr_t& buffer) final {
        if (!value_) {
            return kStreamClosed;
        }
        
        size_t chunk = std::min(buffer.length, 16 - pos_);
        memcpy(buffer.ptr, value_->get_bytes().data() + pos_, chunk);
        buffer += chunk;
        pos_ += chunk;
        
        if (pos_ >= 16) {
            return kStreamClosed;
        } else {
            return kStreamOk;
        }
    }

private:
    Uuid* value_ = nullptr;
    size_t pos_ = 0;
};

}

#undef current_log_topic

#endif // __FIBRE_UUID_CODECS_HPP