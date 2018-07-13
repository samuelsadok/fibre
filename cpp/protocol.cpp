
/* Includes ------------------------------------------------------------------*/

#include <fibre/fibre.hpp>

#include <memory>
#include <stdlib.h>
#include <random>

/* Private defines -----------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Global constant data ------------------------------------------------------*/
/* Global variables ----------------------------------------------------------*/

Endpoint** endpoint_list_ = nullptr; // initialized by calling fibre_publish
size_t n_endpoints_ = 0; // initialized by calling fibre_publish
uint16_t json_crc_; // initialized by calling fibre_publish
JSONDescriptorEndpoint json_file_endpoint_ = JSONDescriptorEndpoint();
EndpointProvider* application_endpoints_;

namespace fibre {
    global_state_t global_state;
}

/* Private constant data -----------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

static inline int write_string(const char* str, fibre::StreamSink* output);

/* Function implementations --------------------------------------------------*/


namespace fibre {

// TODO: remove global variables
/*uint8_t header_buffer_[3];
size_t header_index_ = 0;
uint8_t packet_buffer_[RX_BUF_SIZE];
size_t packet_index_ = 0;
size_t packet_length_ = 0;

bool process_bytes(RemoteNode* node, const uint8_t *buffer, size_t length) {
    while (length--) {
        if (header_index_ < sizeof(header_buffer_)) {
            // Process header byte
            header_buffer_[header_index_++] = *buffer;
            if (header_index_ == 1 && header_buffer_[0] != CANONICAL_PREFIX) {
                header_index_ = 0;
            } else if (header_index_ == 2 && (header_buffer_[1] & 0x80)) {
                header_index_ = 0; // TODO: support packets larger than 128 bytes
            } else if (header_index_ == 3 && calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, header_buffer_, 3)) {
                header_index_ = 0;
            } else if (header_index_ == 3) {
                packet_length_ = header_buffer_[1] + 2;
            }
        } else if (packet_index_ < sizeof(packet_buffer_)) {
            // Process payload byte
            packet_buffer_[packet_index_++] = *buffer;
        }

        // If both header and packet are BUSYy received, hand it on to the packet processor
        if (header_index_ == 3 && packet_index_ == packet_length_) {
            if (calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, packet_buffer_, packet_length_) == 0) {
                if (!process_packet(node, packet_buffer_, packet_length_ - 2))
                    return false;
            }
            header_index_ = packet_index_ = packet_length_ = 0;
        }
        buffer++;
    }

    return true;
}*/

int StreamBasedPacketSink::process_packet(const uint8_t *buffer, size_t length) {
    // TODO: support buffer size >= 128
    if (length >= 128)
        return -1;

    LOG_FIBRE("send header\r\n");
    uint8_t header[] = {
        CANONICAL_PREFIX,
        static_cast<uint8_t>(length),
        0
    };
    header[2] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, header, 2);

    if (output_.process_bytes(header, sizeof(header), nullptr))
        return -1;
    LOG_FIBRE("send payload:\r\n");
    hexdump(buffer, length);
    if (output_.process_bytes(buffer, length, nullptr))
        return -1;

    LOG_FIBRE("send crc16\r\n");
    uint16_t crc16 = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, buffer, length);
    uint8_t crc16_buffer[] = {
        (uint8_t)((crc16 >> 8) & 0xff),
        (uint8_t)((crc16 >> 0) & 0xff)
    };
    if (output_.process_bytes(crc16_buffer, 2, nullptr))
        return -1;
    LOG_FIBRE("sent!\r\n");
    return 0;
}
}


void JSONDescriptorEndpoint::write_json(size_t id, fibre::StreamSink* output) {
    write_string("{\"name\":\"\",", output);

    // write endpoint ID
    write_string("\"id\":", output);
    char id_buf[10];
    snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)id); // TODO: get rid of printf
    write_string(id_buf, output);

    write_string(",\"type\":\"json\",\"access\":\"r\"}", output);
}

void JSONDescriptorEndpoint::register_endpoints(Endpoint** list, size_t id, size_t length) {
    if (id < length)
        list[id] = this;
}

// Returns part of the JSON interface definition.
void JSONDescriptorEndpoint::handle(const uint8_t* input, size_t input_length, fibre::StreamSink* output) {
    // The request must contain a 32 bit integer to specify an offset
    if (input_length < 4)
        return;
    uint32_t offset = read_le<uint32_t>(&input, &input_length);
    fibre::StaticStreamChain<fibre::NullStreamSink, std::reference_wrapper<fibre::StreamSink>>
        output_with_offset(fibre::NullStreamSink(offset), *output);

    size_t id = 0;
    write_string("[", &output_with_offset);
    json_file_endpoint_.write_json(id, &output_with_offset);
    id += decltype(json_file_endpoint_)::endpoint_count;
    write_string(",", &output_with_offset);
    application_endpoints_->write_json(id, &output_with_offset);
    write_string("]", &output_with_offset);
}


namespace fibre {
void publish_function(LocalEndpoint* function) {
    global_state.functions_.push_back(function);
}

void publish_ref_type(FibreRefType* type) {
    global_state.ref_types_.push_back(type);
}

/* Built-in published functions ---------------------------------------------*/
bool get_function_json(uint32_t endpoint_id, char (&output)[256]) {
    printf("%s called with 0x%08x\n", __func__, endpoint_id);

    // TODO: behold, a race condition
    if (endpoint_id >= global_state.functions_.size()) {
        LOG_FIBRE("endpoint_id out of range: %u >= %zu", endpoint_id, global_state.functions_.size());
        return false;
    }
    fibre::LocalEndpoint* endpoint = global_state.functions_[endpoint_id];
    endpoint->get_as_json(output);
    LOG_FIBRE("json in hex:");
    hexdump(reinterpret_cast<uint8_t*>(output), 256);
    return true;
}


constexpr const char get_function_json_function_name[] = "get_function_json";
constexpr const std::tuple<const char (&)[12]> get_function_json_input_names("endpoint_id");
constexpr const std::tuple<const char (&)[5]> get_function_json_output_names("json");
using get_function_json_properties = StaticFunctionProperties<
    decltype(get_function_json_function_name),
    std::remove_const_t<decltype(get_function_json_input_names)>,
    std::remove_const_t<decltype(get_function_json_output_names)>
    >::WithStaticNames<get_function_json_function_name, get_function_json_input_names, get_function_json_output_names>;
auto a = FunctionStuff<std::tuple<uint32_t>, std::tuple<char[256]>, get_function_json_properties>
        //::template WithStaticNames<get_function_json_names>
        ::template WithStaticFuncPtr2<get_function_json>();

/**
 * @brief Runs one scheduler iteration for all remote nodes.
 */
void schedule_all() {
    // TODO: make thread-safe
    LOG_FIBRE("running global scheduler");
    for (std::pair<const Uuid, RemoteNode>& kv : global_state.remote_nodes_) {
        kv.second.schedule();
    }
}

void scheduler_loop() {
    LOG_FIBRE("global scheduler thread started");
    for (;;) {
        global_state.output_pipe_ready.wait();
        //global_state.output_channel_ready.wait();
        schedule_all();
    }
}

/* @brief Initializes Fibre */
void init() {
    if (global_state.initialized) {
        LOG_FIBRE("already initialized");
        return;
    }

    // TODO: global_state should not be constructed statically

    fibre::publish_function(&a);

    std::random_device rd;
    std::uniform_int_distribution<uint8_t> dist;
    uint8_t buffer[16] = { 0 };
    for (size_t i = 0; i < sizeof(buffer); ++i)
        buffer[i] = dist(rd);
    global_state.own_uuid = Uuid(buffer);

#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_GLOBAL_THREAD
    global_state.scheduler_thread = std::thread(scheduler_loop);
    LOG_FIBRE("launched scheduler thread");
    //global_state.scheduler_thread.start();
    // TODO: support shutdown
#else
#error "not implemented"
#endif

    global_state.initialized = true;
}


/*
* @brief Processes a packet originating form a known remote node
* This function returns immediately if all published functions return immediately.
*/
/*bool process_packet(RemoteNode* origin, const uint8_t* buffer, size_t length) {
    if (!origin) {
        LOG_FIBRE("unknown origin");
        return false;
    }
    LOG_FIBRE("got packet of length %zu: \r\n", length);
    hexdump(buffer, length);
    if (length < 4)
        return false;

    // TODO: check CRC
    const size_t per_chunk_overhead = 4;
    while (length >= per_chunk_overhead) {
        uint16_t pipe_no = read_le<uint16_t>(&buffer, &length);
        uint16_t chunk_offset = read_le<uint16_t>(&buffer, &length);
        uint16_t chunk_crc = read_le<uint16_t>(&buffer, &length);
        uint16_t chunk_length = read_le<uint16_t>(&buffer, &length);
        bool close_pipe = (chunk_length & 1);
        chunk_length >>= 1;
        LOG_FIBRE("pipe %d, chunk %x - %x\n", pipe_no, chunk_offset, chunk_offset + chunk_length - 1);

        if (chunk_length > length) {
            LOG_FIBRE("chunk longer than packet");
            return false;
        }
        //InputPipe& pipe = pipes_[pipe_no];
        pipe->process_chunk(buffer, chunk_offset, chunk_length, chunk_crc, close_pipe);
        buffer += chunk_length;
        length -= chunk_length;
    }

    // The packet should be fully consumed now
    if (length)
        return false;

    return true;*/



/*    uint16_t seq_no = read_le<uint16_t>(&buffer, &length);

    if (seq_no & 0x8000) {
        // TODO: ack handling
    } else {
        // TODO: think about some kind of ordering guarantees
        // currently the seq_no is just used to associate a response with a request

        uint16_t endpoint_id = read_le<uint16_t>(&buffer, &length);
        bool expect_response = endpoint_id & 0x8000;
        endpoint_id &= 0x7fff;

        // TODO: behold, a race condition
        if (endpoint_id >= fibre::functions_.size())
            return -1;
        fibre::LocalEndpoint* endpoint = fibre::functions_[endpoint_id];

        if (!endpoint) {
            LOG_FIBRE("critical: no endpoint at %d", endpoint_id);
            return -1;
        }

        // Verify packet trailer. The expected trailer value depends on the selected endpoint.
        // For endpoint 0 this is just the protocol version, for all other endpoints it's a
        // CRC over the entire JSON descriptor tree (this may change in future versions).
        uint16_t expected_trailer = endpoint_id ? json_crc_ : PROTOCOL_VERSION;
        uint16_t actual_trailer = buffer[length - 2] | (buffer[length - 1] << 8);
        if (expected_trailer != actual_trailer) {
            LOG_FIBRE("trailer mismatch for endpoint %d: expected %04x, got %04x\r\n", endpoint_id, expected_trailer, actual_trailer);
            return -1;
        }
        LOG_FIBRE("trailer ok for endpoint %d\r\n", endpoint_id);

        // TODO: if more bytes than the MTU were requested, should we abort or just return as much as possible?

        uint16_t expected_response_length = read_le<uint16_t>(&buffer, &length);

        // Limit response length according to our local TX buffer size
        if (expected_response_length > sizeof(tx_buf_) - 2)
            expected_response_length = sizeof(tx_buf_) - 2;

        MemoryStreamSource input(buffer, length - 2);
        MemoryStreamSink intermediate_output(tx_buf_ + 2, expected_response_length); // TODO: remove this
        endpoint->invoke(&input, &intermediate_output);

        // Send response
        if (expect_response) {
            size_t actual_response_length = expected_response_length - intermediate_output.get_free_space() + 2;
            write_le<uint16_t>(seq_no | 0x8000, tx_buf_);

            LOG_FIBRE("send packet:\r\n");
            hexdump(tx_buf_, actual_response_length);
            output.process_packet(tx_buf_, actual_response_length);
        }
    }

    return 0;*/
//}





/*template<TDecoder, TStorage>
class DecoderWithStorage<() {
    TDecoder decoder;
    TStorage storage;
}*/

template<size_t s> struct incomplete;

IncomingConnectionDecoder::status_t IncomingConnectionDecoder::advance_state() {
    switch (state_) {
        case RECEIVING_HEADER: {
                HeaderDecoderChain *header_decoder = get_stream<HeaderDecoderChain>();
                uint16_t endpoint_id = header_decoder->get_stream<0>().get_value();
                uint16_t endpoint_hash = header_decoder->get_stream<1>().get_value();
                LOG_FIBRE("finished receiving header: endpoint %04x, hash %04x", endpoint_id, endpoint_hash);
                
                // TODO: behold, a race condition
                if (endpoint_id >= global_state.functions_.size())
                    return ERROR;
                endpoint_ = global_state.functions_[endpoint_id];

                if (!endpoint_) {
                    LOG_FIBRE("critical: no endpoint at %d", endpoint_id);
                    return ERROR;
                }

                // Verify endpoint hash. The expected hash value depends on the selected endpoint.
                // For endpoint 0 this is just the protocol version, for all other endpoints it's a
                // CRC over the entire JSON descriptor tree (this may change in future versions).
                uint16_t expected_hash = endpoint_->get_hash();
                if (expected_hash != endpoint_hash) {
                    LOG_FIBRE("hash mismatch for endpoint %d: expected %04x, got %04x\r\n", endpoint_id, expected_hash, endpoint_hash);
                    return ERROR;
                }
                LOG_FIBRE("hash ok for endpoint %d\r\n", endpoint_id);

                endpoint_->open_connection(*this);
                //set_stream(nullptr);
                // TODO: make sure the start_connection call invokes set_stream
                state_ = RECEIVING_PAYLOAD;
                return OK;
            }
        case RECEIVING_PAYLOAD:
            LOG_FIBRE("finished receiving payload");
            if (endpoint_)
                endpoint_->decoder_finished(*this, output_pipe_);
            set_stream(nullptr);
            return CLOSED;
        default:
            set_stream(nullptr);
            return ERROR;
    }
}

// o -----------> o
// o <----------- o

// outgoing: input...output
// incoming: input...output


//int asd() {
//incomplete<sizeof(IncomingConnectionDecoder)> a;
//}
static_assert(sizeof(IncomingConnectionDecoder) == RX_BUF_SIZE, "Something is off. Please fix.");

InputPipe* RemoteNode::get_input_pipe(size_t id, bool* is_new) {
    std::unordered_map<size_t, InputPipe>& input_pipes = server_input_pipes_;
    // TODO: limit number of concurrent pipes
    auto emplace_result = input_pipes.emplace(id, id);
    InputPipe& input_pipe = emplace_result.first->second;
    if (is_new)
        *is_new = emplace_result.second;
    return &input_pipe;
}

OutputPipe* RemoteNode::get_output_pipe(size_t id) {
    std::unordered_map<size_t, OutputPipe>& output_pipes = server_output_pipes_;
    // TODO: only return unused pipes
    auto emplace_result = output_pipes.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(id),
        std::forward_as_tuple(this, id));
    OutputPipe& output_pipe = emplace_result.first->second;
    return &output_pipe;
}

void RemoteNode::schedule() {
    constexpr size_t per_packet_overhead = 16 + 2;
    constexpr size_t per_chunk_overhead = 8;
    
    LOG_FIBRE("dispatching output chunks...");

    for (OutputChannel* channel : output_channels_) {
        //output_stream = channel;
        size_t free_space = channel->get_min_non_blocking_bytes();
        if (free_space < per_packet_overhead)
            continue;
        size_t processed_bytes = 0;

        for (std::pair<const size_t, OutputPipe>& pipe_entry : server_output_pipes_) { // TODO: include client
            OutputPipe& pipe = pipe_entry.second;

            monotonic_time_t due_time = pipe.get_due_time();
            if (due_time > now())
                continue;

            for (OutputPipe::chunk_t chunk : pipe.get_pending_chunks()) {
                size_t max_chunk_len = channel->get_min_non_blocking_bytes();
                if (max_chunk_len < per_chunk_overhead)
                    break;
                max_chunk_len = max_chunk_len != SIZE_MAX ? max_chunk_len - per_chunk_overhead : max_chunk_len;

                // send chunk header
                size_t offset = 0, length = 0;
                uint16_t crc_init = 0;
                if (!chunk.get_properties(&offset, &length, &crc_init)) {
                    LOG_FIBRE("get_properties failed");
                }
                length = std::min(length, max_chunk_len);
                uint8_t buffer[8];
                write_le<uint16_t>(pipe.get_id(), buffer, 2);
                write_le<uint16_t>(offset, buffer + 2, 2);
                write_le<uint16_t>(crc_init, buffer + 4, 2);
                write_le<uint16_t>(length, buffer + 6, 2);
                LOG_FIBRE("emitting pipe id %04zx chunk %04zx - %04zx, crc %04x", pipe.get_id(), offset, length - 1, crc_init);
                size_t processed_bytes = 0;
                if (channel->process_bytes(buffer, sizeof(buffer), &processed_bytes) != StreamSink::OK) {
                    LOG_FIBRE("channel failed");
                    // TODO: remove channel
                    break;
                }
                if (processed_bytes != sizeof(buffer)) {
                    LOG_FIBRE("expected to process %08zx bytes but only processed %08zx bytes", sizeof(buffer), processed_bytes);
                    break;
                }

                // send chunk paylaod
                if (!chunk.write_to(channel, length)) {
                    LOG_FIBRE("the chunk could not be fully sent - get_min_non_blocking_bytes lied to us.");
                    break;
                }

                if (!pipe.guaranteed_delivery) {
                    pipe.drop_chunk(offset, length);
                } else {
                    due_time = std::max(due_time + channel->resend_interval, now());
                    pipe.set_due_time(offset, length, due_time);
                }
            }

            auto chunks = pipe.get_pending_chunks();
            if (chunks.begin() < chunks.end())
                notify_output_pipe_ready();
        }
    }
}

/**
 * @brief Notifies the scheduler that there is data in at least one output
 * pipe.
 */
void RemoteNode::notify_output_pipe_ready() {
#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_GLOBAL_THREAD
    global_state.output_pipe_ready.set();
#elif CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_PER_NODE_THREAD
    output_pipe_ready_.set();
#endif
}

/**
 * @brief Notifies the scheduler that at least one output channel can
 * consume data without blocking.
 */
void RemoteNode::notify_output_channel_ready() {
#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_GLOBAL_THREAD
    global_state.output_channel_ready.set();
#elif CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_PER_NODE_THREAD
    output_channel_ready_.set();
#endif
}

RemoteNode* get_remote_node(Uuid uuid) {
    // TODO: limit number of concurrent nodes and garbage collect abandonned nodes
    std::pair<const Uuid, RemoteNode>& remote_node = *(global_state.remote_nodes_.emplace(uuid, uuid).first);
    return &(remote_node.second);
}

OutputPipe::status_t OutputPipe::process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) {
    LOG_FIBRE("processing %zx bytes", length);
    size_t chunk = std::min(length, sizeof(buffer_) - buffer_pos_);
    memcpy(buffer_ + buffer_pos_, buffer, chunk);
    buffer_pos_ += chunk;
    if (processed_bytes)
        *processed_bytes += chunk;
    remote_node_->notify_output_pipe_ready();
    return chunk < length ? BUSY : OK;
}

}
