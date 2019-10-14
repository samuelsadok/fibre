
#include "uuid.hpp"
#include "decoder.hpp"
#include "encoder.hpp"

namespace fibre {

class BigEndianUuidDecoder : public Decoder<Uuid> {
public:
    virtual status_t process_bytes(cbufptr_t& buffer) final {
        size_t chunk = std::min(buffer.length, sizeof(buf_) - pos_);
        memcpy(buf_ + pos_, buffer.ptr, chunk);
        buffer += chunk;
        pos_ += chunk;
        
        if (pos_ >= 16) {
            uuid_ = Uuid{buf_};
            return StreamSink::CLOSED;
        } else {
            return StreamSink::OK;
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

    virtual status_t get_bytes(bufptr_t& buffer) final {
        if (!value_) {
            return StreamSource::CLOSED;
        }
        
        size_t chunk = std::min(buffer.length, 16 - pos_);
        memcpy(buffer.ptr, value_->get_bytes().data() + pos_, chunk);
        buffer += chunk;
        pos_ += chunk;
        
        if (pos_ >= 16) {
            return StreamSource::CLOSED;
        } else {
            return StreamSource::OK;
        }
    }

private:
    Uuid* value_ = nullptr;
    size_t pos_ = 0;
};

}
