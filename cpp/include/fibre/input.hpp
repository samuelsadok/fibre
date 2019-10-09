#ifndef __FIBRE_INPUT_HPP
#define __FIBRE_INPUT_HPP

#include <fibre/stream.hpp>
#include <fibre/decoders.hpp>
#include <fibre/uuid.hpp>
#include <fibre/logging.hpp>
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

/**
 * @brief Can be fed chunks of bytes in a random order.
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

    status_t get_buffer(const uint8_t** buf, size_t* length) final {
        if (buf)
            *buf = buf_ + (read_ptr_ % I);
        if (length)
            *length = count_valid_table(read_ptr_ % I, std::min(*length, I - (read_ptr_ % I)));
        return OK;
    }

    status_t consume(size_t length) final {
        FIBRE_LOG(D) << "consume " << length << " bytes";
        clear_valid_table(read_ptr_ % I, length);
        read_ptr_ += length;
        return OK;
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
    size_t count_valid_table(size_t offset, size_t length) {
        FIBRE_LOG(D) << "counting valid table from " << offset << ", length " << length;
        for (size_t bit = offset; bit < offset + length; ++bit) {
            if (!(valid_table_[bit / bits_per_int] >> (bit % bits_per_int))) {
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




using call_id_t = Uuid;

struct call_t {
    StreamSink* decoder;
    // TODO: what if we want a buffer size that depends on the call context?
    FixedBufferDefragmenter<1024> fragment_sink;
};

// TODO: replace with fixed size data structure
using fragmented_calls_t = std::unordered_map<call_id_t, call_t>;
extern fragmented_calls_t fragmented_calls;

static int start_or_get_call(call_id_t call_id, call_t** call) {
    call_t* dummy;
    call = call ? call : &dummy;
    *call = &fragmented_calls[call_id];
    return 0;
}

class CallIdDecoder : Decoder<call_id_t> {
    
};

class CallDecoder : public StreamSink {
private:
    Decoder<size_t>* offset_decoder = nullptr;
    call_t* call_ = nullptr;
};

class FragmentedCallDecoder : public StreamSink {
    status_t process_bytes(cbufptr_t& buffer) final {
        status_t status;
        if (pos_ == 0) {
            if ((status = call_id_decoder->process_bytes(buffer)) != CLOSED) {
                return status;
            }
            const call_id_t* call_id = call_id_decoder->get();
            if (call_id != nullptr) {
                start_or_get_call(*call_id, &call_);
            }
            pos_++;
        }
        if (pos_ == 1) {
            if ((status = offset_decoder->process_bytes(buffer)) != CLOSED) {
                return status;
            }
            offset_ = *offset_decoder->get();
            pos_++;
        }
        if (pos_ == 2) {
            call_->fragment_sink.process_chunk(buffer, offset_);
        }
        return OK;
    }

private:
    size_t pos_ = 0;
    Decoder<call_id_t>* call_id_decoder = nullptr;
    Decoder<size_t>* offset_decoder = nullptr;
    call_t* call_ = nullptr;
    size_t offset_ = 0;
};

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
