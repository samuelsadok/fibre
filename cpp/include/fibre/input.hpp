#ifndef __FIBRE_INPUT_HPP
#define __FIBRE_INPUT_HPP

#include <fibre/stream.hpp>
#include <fibre/decoder.hpp>
#include <fibre/encoder.hpp>
#include <fibre/context.hpp>
#include <fibre/uuid.hpp>
#include <fibre/logging.hpp>
#include <fibre/local_function.hpp>
#include <limits.h>

DEFINE_LOG_TOPIC(INPUT);
#define current_log_topic LOG_TOPIC_INPUT

namespace fibre {

/**
 * @brief Can be fed chunks of bytes in a random order.
 */
class Defragmenter {
public:
    /**
     * @brief Processes a chunk of data.
     * 
     * @param buffer: The chunk of data to process.
     *        The buffer will be advanced to reflect which bytes the caller can
     *        consider as handled (or cached). This includes data that was
     *        already known from a previous call to this function.
     *        If the defragmenter decides to handle/cache bytes in the middle of
     *        the chunk (as opposed to the beginning), this will not be reflected
     *        to the caller.
     *        The only reason why not all of the chunk would be processed
     *        is that the internal buffer is (temporarily) full.
     */
    virtual void process_chunk(cbufptr_t& buffer, size_t offset) = 0;
};

class Fragmenter {
public:
    virtual void get_chunk(cbufptr_t& buffer, size_t* offset) = 0;

    /**
     * @brief Marks the given range as acknowledged, meaning that the data will
     * be discarded.
     * 
     * If the stream sink was previously full, this might clear up new space.
     */
    virtual void acknowledge_chunk(size_t offset, size_t length) = 0;
};

/**
 * @brief Implements a very simple defragmenter with a fixed size internal buffer.
 */
template<size_t I>
class FixedBufferDefragmenter : public Defragmenter, public OpenStreamSource {
public:
    void process_chunk(cbufptr_t& buffer, size_t offset) final {
        // prune start of chunk
        if (offset < read_ptr_) {
            size_t diff = read_ptr_ - offset;
            if (diff >= buffer.length) {
                buffer += buffer.length;
                return; // everything in this chunk was already received and consumed before
            }
            FIBRE_LOG(D) << "discarding " << diff << " bytes at beginning of chunk";
            buffer += diff;
            offset += diff;
        }

        // prune end of chunk
        if (offset + buffer.length > read_ptr_ + I) {
            size_t diff = offset + buffer.length - (read_ptr_ + I);
            if (diff >= buffer.length) {
                return; // the chunk starts so far into the future that we can't use any of it
            }
            FIBRE_LOG(D) << "discarding " << diff << " bytes at end of chunk";
            buffer.length -= diff;
        }

        size_t dst_offset = (offset % I);

        // copy usable part of chunk to internal buffer
        // may need two copy operations because buf_ is a circular buffer
        if (buffer.length > I - dst_offset) {
            memcpy(buf_ + dst_offset, buffer.ptr, I - dst_offset);
            set_valid_table(dst_offset, I - dst_offset);
            buffer += (I - dst_offset);
            dst_offset = 0;
        }
        memcpy(buf_ + dst_offset, buffer.ptr, buffer.length);
        set_valid_table(dst_offset, buffer.length);
        buffer += buffer.length;
    }

    status_t get_buffer(cbufptr_t* buf) final {
        if (buf) {
            buf->ptr = buf_ + (read_ptr_ % I);
            buf->length = count_valid_table(1, read_ptr_ % I, std::min(buf->length, I - (read_ptr_ % I)));
        }
        return kOk;
    }

    status_t consume(size_t length) final {
        FIBRE_LOG(D) << "consume " << length << " bytes";
        clear_valid_table(read_ptr_ % I, length);
        read_ptr_ += length;
        return count_valid_table(1, read_ptr_ % I, 1) ? kOk : kBusy;
    }

private:
    static constexpr size_t bits_per_int = sizeof(unsigned int) * CHAR_BIT;

    /** @brief Flips a consecutive chunk of bits in valid_table_ to 1 */
    void set_valid_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            valid_table_[bit / bits_per_int] |= ((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Flips a consecutive chunk of bits in valid_table_ to 0 */
    void clear_valid_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            valid_table_[bit / bits_per_int] &= ~((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Counts how many bits consecutive bits in valid_table_ are 1, starting at offset */
    size_t count_valid_table(unsigned int expected_val, size_t offset, size_t length) {
        FIBRE_LOG(D) << "counting valid table from " << offset << ", length " << length;
        for (size_t bit = offset; bit < offset + length; ++bit) {
            if (((valid_table_[bit / bits_per_int] >> (bit % bits_per_int)) & 1) != expected_val) {
                FIBRE_LOG(D) << "invalid at " << bit;
                return bit - offset;
            }
        }
        FIBRE_LOG(D) << "all " << length << " valid";
        return length;
    }

    unsigned int valid_table_[(I + bits_per_int - 1) / bits_per_int] = {0};
    uint8_t buf_[I];
    size_t read_ptr_ = 0;
};

/**
 * @brief Implements a very simple fragmenter with a fixed size internal buffer.
 * 
 * This fragmenter always emits the chunk which represents the oldest data that
 * wasn't acknowledged yet.
 */
template<size_t I>
class FixedBufferFragmenter : public Fragmenter, public OpenStreamSink {
public:
    void get_chunk(cbufptr_t& buffer, size_t* offset) final {
        size_t read_ptr = write_ptr_ - I + count_fresh_table(0, write_ptr_ % I, I - (write_ptr_ % I));
        if ((read_ptr % I) == 0) {
            read_ptr += count_fresh_table(0, 0, write_ptr_ - read_ptr);
        }
        
        buffer = {
            .ptr = &buf_[read_ptr % I],
            .length = count_fresh_table(1, read_ptr % I, std::min(buffer.length, I - (read_ptr % I))) % I
        };
        if (offset)
            *offset = read_ptr;
    }

    void acknowledge_chunk(size_t offset, size_t length) final {
        // prune end of chunk
        if (offset + length > write_ptr_) {
            size_t diff = offset + length - write_ptr_;
            FIBRE_LOG(E) << "received ack for future bytes";
            if (diff >= length) {
                return; // the ack starts so far into the future that we can't use any of it
            }
            length -= diff;
        }

        // prune start of chunk
        if (offset + I < write_ptr_) {
            size_t diff = write_ptr_ - (offset + I);
            if (diff >= length) {
                return; // everything in this chunk was already acknowledged before
            }
            FIBRE_LOG(D) << "received redundant ack for " << diff << " bytes";
            offset += diff;
            length -= diff;
        }

        FIBRE_LOG(D) << "received ack for " << length << " bytes";

        size_t dst_offset = (offset % I);
        
        if (length > I - dst_offset) {
            clear_fresh_table(dst_offset, I - dst_offset);
            length -= (I - dst_offset);
            dst_offset = 0;
        }
        clear_fresh_table(dst_offset, length);
    }

    status_t get_buffer(bufptr_t* buf) final {
        if (buf) {
            buf->ptr = buf_ + (write_ptr_ % I);
            buf->length = count_fresh_table(0, write_ptr_ % I, std::min(buf->length, I - (write_ptr_ % I)));
        }
        return kOk;
    }

    status_t commit(size_t length) final {
        FIBRE_LOG(D) << "commit " << length << " bytes";
        set_fresh_table(write_ptr_ % I, length);
        write_ptr_ += length;
        return kOk;
    }

private:
    static constexpr size_t bits_per_int = sizeof(unsigned int) * CHAR_BIT;

    /** @brief Flips a consecutive chunk of bits in fresh_table_ to 1 */
    void set_fresh_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            fresh_table_[bit / bits_per_int] |= ((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Flips a consecutive chunk of bits in fresh_table_ to 0 */
    void clear_fresh_table(size_t offset, size_t length) {
        for (size_t bit = offset; bit < offset + length; ++bit) {
            fresh_table_[bit / bits_per_int] &= ~((unsigned int)1 << (bit % bits_per_int));
        }
    }

    /** @brief Counts how many bits consecutive bits in fresh_table_ are 1, starting at offset */
    size_t count_fresh_table(unsigned int expected_val, size_t offset, size_t length) {
        FIBRE_LOG(D) << "counting fresh table from " << offset << ", length " << length;
        for (size_t bit = offset; bit < offset + length; ++bit) {
            if (((fresh_table_[bit / bits_per_int] >> (bit % bits_per_int)) & 1) != expected_val) {
                FIBRE_LOG(D) << "non-match at " << bit;
                return bit - offset;
            }
        }
        FIBRE_LOG(D) << "all " << length << " fresh";
        return length;
    }

    unsigned int fresh_table_[(I + bits_per_int - 1) / bits_per_int] = {0}; // marks which bytes are fresh (valid and not yet acknowledged)
    uint8_t buf_[I];
    size_t write_ptr_ = 0;
};


class CallDecoder : public StreamSink {
public:
    CallDecoder(Context* ctx)
        : ctx_(ctx), endpoint_id_decoder_(alloc_decoder<Uuid>(ctx))
    {}

    ~CallDecoder() {
        // TODO: provide close function to dealloc decoders
        /*if (endpoint_id_decoder_) {
            dealloc_decoder(endpoint_id_decoder_);
            endpoint_id_decoder_ = nullptr;
        }*/
    }

    status_t process_bytes(cbufptr_t& buffer) final {
        if (endpoint_id_decoder_) {
            status_t status;
            if ((status = endpoint_id_decoder_->process_bytes(buffer)) != kClosed) {
                return status;
            }
            FIBRE_LOG(D) << "decoded endpoint ID: " << *endpoint_id_decoder_->get();
            endpoint_ = get_endpoint(*endpoint_id_decoder_->get());
            if (!endpoint_) {
                FIBRE_LOG(W) << "received call for unknown endpoint ID " << *endpoint_id_decoder_->get();
            }
            dealloc_decoder(endpoint_id_decoder_);
            endpoint_id_decoder_ = nullptr;
            decoder_ = endpoint_ ? endpoint_->open(ctx_) : nullptr;
        }
        if (endpoint_ && decoder_) {
            return decoder_->process_bytes(buffer);
        } else {
            return kError;
        }
    }

private:
    //Uuid endpoint_id_{};
    bool have_endpoint_id_ = false;
    Decoder<Uuid>* endpoint_id_decoder_ = nullptr;
    LocalEndpoint* endpoint_ = nullptr;
    StreamSink* decoder_ = nullptr;
    Context* ctx_;
};

class CallEncoder : public StreamSource {
public:
    CallEncoder(Context* ctx, Uuid remote_endpoint_id, StreamSource* encoder) :
            ctx_(ctx), remote_endpoint_id_(remote_endpoint_id), encoder_(encoder) {
        endpoint_id_encoder_ = alloc_encoder<Uuid>(ctx);
        endpoint_id_encoder_->set(&remote_endpoint_id_);
    }

    status_t get_bytes(bufptr_t& buffer) final {
        StreamSource::status_t status;
        if (endpoint_id_encoder_) {
            if ((status = endpoint_id_encoder_->get_bytes(buffer)) != StreamSource::kClosed) {
                return status;
            }
            dealloc_encoder(endpoint_id_encoder_);
            endpoint_id_encoder_ = nullptr;
        }
        return encoder_->get_bytes(buffer);
    }

private:
    Uuid remote_endpoint_id_;
    Context* ctx_;
    Encoder<Uuid>* endpoint_id_encoder_ = nullptr;
    StreamSource* encoder_ = nullptr;
};


using call_id_t = Uuid;


struct incoming_call_t {
    CallDecoder decoder; // TODO: put actual call decoder (call id + forward content)
    // TODO: what if we want a buffer size that depends on the call context?
    FixedBufferDefragmenter<1024> fragment_sink;
};

struct outgoing_call_t {
    Uuid uuid;
    Context* ctx;
    CallEncoder encoder; // TODO: put actual call encoder (call id + forward content)
    FixedBufferFragmenter<1024> fragment_source;
};




/*
class FragmentedCallDecoder : public StreamSink {
public:
    FragmentedCallDecoder(Context* ctx) : ctx_(ctx) {}

    status_t process_bytes(cbufptr_t& buffer) final {
        status_t status;
        if (pos_ == 0) {
            if ((status = call_id_decoder->process_bytes(buffer)) != kClosed) {
                return status;
            }
            const call_id_t* call_id = call_id_decoder->get();
            if (call_id != nullptr) {
                start_or_get_call(ctx_, *call_id, &call_);
            }
            pos_++;
        }
        if (pos_ == 1) {
            if ((status = offset_decoder->process_bytes(buffer)) != kClosed) {
                return status;
            }
            offset_ = *offset_decoder->get();
            pos_++;
        }
        if (pos_ == 2) {
            call_->fragment_sink.process_chunk(buffer, offset_);
            stream_copy(&call_->decoder, &call_->fragment_sink);
        }
        return kOk;
    }

private:
    Context* ctx_ = nullptr;
    size_t pos_ = 0;
    Decoder<call_id_t>* call_id_decoder = nullptr;
    Decoder<size_t>* offset_decoder = nullptr;
    incoming_call_t* call_ = nullptr;
    size_t offset_ = 0;
};*/

int decode_fragment(Context* ctx, StreamSource* source);
int encode_fragment(outgoing_call_t call, StreamSink* sink);

/*class FragmentedCallEncoder : public StreamSource {
public:
    status_t get_bytes(bufptr_t& buffer) {
        status_t status;
        if (pos_ == 0) {
            if ((status = call_id_decoder->get_bytes(buffer)) != kClosed) {
                return status;
            }
            const call_id_t* call_id = call_id_decoder->get();
            if (call_id != nullptr) {
                start_or_get_call(*call_id, &call_);
            }
            pos_++;
        }
        if (pos_ == 1) {
            if ((status = offset_decoder->get_bytes(buffer)) != kClosed) {
                return status;
            }
            offset_ = *offset_decoder->get();
            pos_++;
        }
        if (pos_ == 2) {
            call_->fragment_sink.process_chunk(buffer, offset_);
            stream_copy(call_->decoder, &call_->fragment_sink);
        }
    }

private:
    size_t pos_ = 0;
    size_t offset_ = 0;
};*/

#if 0

class InputPipe {
    // don't allow copy/move (we don't know if it's save to relocate the buffer)
    InputPipe(const InputPipe&) = delete;
    InputPipe& operator=(const InputPipe&) = delete;

    // TODO: use Decoder infrastructure to process incoming bytes
    uint8_t rx_buf_[RX_BUF_SIZE];
    //uint8_t tx_buf_[TX_BUF_SIZE]; // TODO: this does not belong here
    size_t pos_ = 0;
    bool at_packet_break = true;
    size_t crc_ = CANONICAL_CRC16_INIT;
    size_t total_length_ = 0;
    bool total_length_known = false;
    size_t id_; // last bit indicates server (0) or client (1)
    StreamSink* input_handler_ = nullptr; // TODO: destructor
    OutputPipe* output_pipe_ = nullptr; // must be set immediately after constructor call
public:
    InputPipe(RemoteNode* remote_node, size_t idx, bool is_server)
        : id_((idx << 1) | (is_server ? 0 : 1)) {}

    size_t get_id() const { return id_; }
    void set_output_pipe(OutputPipe* output_pipe) { output_pipe_ = output_pipe; }

    template<typename TDecoder, typename ... TArgs>
    void construct_decoder(TArgs&& ... args) {
        set_handler(nullptr);
        static_assert(sizeof(TDecoder) <= RX_BUF_SIZE, "TDecoder is too large. Increase the buffer size of this pipe.");
        set_handler(new (rx_buf_) TDecoder(std::forward<TArgs>(args)...));
    }
    void set_handler(StreamSink* new_handler) {
        if (input_handler_)
            input_handler_->~StreamSink();
        input_handler_ = new_handler;
    }

    void process_chunk(const uint8_t* buffer, size_t offset, size_t length, uint16_t crc, bool packet_break);
};


class InputChannelDecoder /*: public StreamSink*/ {
public:
    InputChannelDecoder(RemoteNode* remote_node) :
        remote_node_(remote_node),
        header_decoder_(make_header_decoder())
        {}
    
    StreamSink::status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes);
    size_t get_min_useful_bytes() const;
private:
    using HeaderDecoder = StaticStreamChain<
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>>;
    RemoteNode* remote_node_;
    HeaderDecoder header_decoder_;
    bool in_header = true;

    static HeaderDecoder make_header_decoder() {
        return HeaderDecoder(
                FixedIntDecoder<uint16_t, false>(),
                FixedIntDecoder<uint16_t, false>(),
                FixedIntDecoder<uint16_t, false>(),
                FixedIntDecoder<uint16_t, false>()
        );
    }

    uint16_t& get_pipe_no() { return header_decoder_.get_stream<0>().get_value(); }
    uint16_t& get_chunk_offset() { return header_decoder_.get_stream<1>().get_value(); }
    uint16_t& get_chunk_crc() { return header_decoder_.get_stream<2>().get_value(); }
    uint16_t& get_chunk_length() { return header_decoder_.get_stream<3>().get_value(); }

    void reset() {
        header_decoder_ = make_header_decoder();
        in_header = true;
    }
};

class IncomingConnectionDecoder : public DynamicStreamChain<RX_BUF_SIZE - 52> {
public:
    IncomingConnectionDecoder(OutputPipe& output_pipe) : output_pipe_(&output_pipe) {
        set_stream<HeaderDecoderChain>(FixedIntDecoder<uint16_t, false>(), FixedIntDecoder<uint16_t, false>());
    }
    template<typename TDecoder, typename ... TArgs>
    void set_stream(TArgs&& ... args) {
        DynamicStreamChain::set_stream<TDecoder, TArgs...>(std::forward<TArgs>(args)...);
    }
    void set_stream(StreamSink* new_stream) {
        DynamicStreamChain::set_stream(new_stream);
    }
    template<typename TDecoder>
    TDecoder* get_stream() {
        return DynamicStreamChain::get_stream<TDecoder>();
    }
    template<typename TDecoder>
    const TDecoder* get_stream() const {
        return DynamicStreamChain::get_stream<TDecoder>();
    }
private:
    enum {
        RECEIVING_HEADER,
        RECEIVING_PAYLOAD
    } state_ = RECEIVING_HEADER;
    const LocalEndpoint* endpoint_ = nullptr;
    OutputPipe* output_pipe_ = nullptr;

    using HeaderDecoderChain = StaticStreamChain<
                FixedIntDecoder<uint16_t, false>,
                FixedIntDecoder<uint16_t, false>
            >; // size: 96 bytes

    status_t advance_state() final;
};

//template<size_t s> struct incomplete;
//int asd() {
//incomplete<sizeof(IncomingConnectionDecoder)> a;
//}
static_assert(sizeof(IncomingConnectionDecoder) == RX_BUF_SIZE, "Something is off. Please fix.");
#endif

}

#undef current_log_topic

#endif // __FIBRE_INPUT_HPP
