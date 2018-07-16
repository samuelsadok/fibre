#ifndef __FIBRE_INPUT_HPP
#define __FIBRE_INPUT_HPP

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

namespace fibre {

class InputPipe {
    // don't allow copy/move (we don't know if it's save to relocate the buffer)
    InputPipe(const InputPipe&) = delete;
    InputPipe& operator=(const InputPipe&) = delete;

    // TODO: use Decoder infrastructure to process incoming bytes
    uint8_t rx_buf_[RX_BUF_SIZE];
    //uint8_t tx_buf_[TX_BUF_SIZE]; // TODO: this does not belong here
    size_t pos_ = 0;
    size_t crc_ = CANONICAL_CRC16_INIT;
    size_t total_length_ = 0;
    bool total_length_known = false;
    size_t id_;
    StreamSink* input_handler = nullptr; // TODO: destructor
public:
    InputPipe(size_t id) : id_(id) {}

    template<typename TDecoder, typename ... TArgs>
    void construct_decoder(TArgs&& ... args) {
        static_assert(sizeof(TDecoder) <= RX_BUF_SIZE, "TDecoder is too large. Increase the buffer size of this pipe.");
        input_handler = new (rx_buf_) TDecoder(std::forward<TArgs>(args)...);
    }

    void process_chunk(const uint8_t* buffer, size_t offset, size_t length, uint16_t crc, bool close_pipe);

    void close() {
        LOG_FIBRE_W(INPUT, "close pipe not implemented");
    }
    void packet_reset() {
        pos_ = 0;
        crc_ = CANONICAL_CRC16_INIT;
    }
};

class InputChannelDecoder : public StreamSink {
public:
    InputChannelDecoder(RemoteNode* remote_node) :
        remote_node_(remote_node),
        header_decoder_(make_header_decoder())
        {}
    
    status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) final;
private:
    using HeaderDecoder = StaticStreamChain<
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>>;
    RemoteNode* remote_node_;
    InputPipe* input_pipe_;
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
        input_pipe_ = nullptr;
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
    LocalEndpoint* endpoint_ = nullptr;
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

}

#endif // __FIBRE_INPUT_HPP
