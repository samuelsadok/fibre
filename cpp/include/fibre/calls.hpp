#ifndef __FIBRE_CALLS_HPP
#define __FIBRE_CALLS_HPP

#include <fibre/stream.hpp>
#include <fibre/decoder.hpp>
#include <fibre/encoder.hpp>
#include <fibre/context.hpp>
#include <fibre/uuid.hpp>
#include <fibre/logging.hpp>
#include <fibre/local_endpoint.hpp>
#include <fibre/fragmenter.hpp>

DEFINE_LOG_TOPIC(CALLS);
#define current_log_topic LOG_TOPIC_CALLS

namespace fibre {

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
    Context ctx;
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

#endif // __FIBRE_CALLS_HPP
