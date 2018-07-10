
/* Includes ------------------------------------------------------------------*/

#include <memory>
#include <stdlib.h>

#include <fibre/fibre.hpp>

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
std::vector<fibre::FibreRefType*> ref_types_ = std::vector<fibre::FibreRefType*>();
std::vector<fibre::LocalEndpoint*> functions_ = std::vector<fibre::LocalEndpoint*>();
}

/* Private constant data -----------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

static void hexdump(const uint8_t* buf, size_t len);
static inline int write_string(const char* str, fibre::StreamSink* output);

/* Function implementations --------------------------------------------------*/

#if 0
void hexdump(const uint8_t* buf, size_t len) {
    for (size_t pos = 0; pos < len; ++pos) {
        printf(" %02x", buf[pos]);
        if ((((pos + 1) % 16) == 0) || ((pos + 1) == len))
            printf("\r\n");
        osDelay(2);
    }
}
#else
void hexdump(const uint8_t* buf, size_t len) {
    (void) buf;
    (void) len;
}
#endif


namespace fibre {

// TODO: remove global variables
uint8_t header_buffer_[3];
size_t header_index_ = 0;
uint8_t packet_buffer_[RX_BUF_SIZE];
size_t packet_index_ = 0;
size_t packet_length_ = 0;

template<typename TInputChannel>
int process_bytes(TInputChannel& input_channel, const uint8_t *buffer, size_t length, size_t* processed_bytes, PacketSink& output) {
    int result = 0;

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

        // If both header and packet are fully received, hand it on to the packet processor
        if (header_index_ == 3 && packet_index_ == packet_length_) {
            if (calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, packet_buffer_, packet_length_) == 0) {
                result |= input_channel.process_packet(packet_buffer_, packet_length_ - 2);
            }
            header_index_ = packet_index_ = packet_length_ = 0;
        }
        buffer++;
        if (processed_bytes)
            (*processed_bytes)++;
    }

    return result;
}

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
    functions_.push_back(function);
}

/* Built-in published functions ---------------------------------------------*/
bool get_function_json(uint32_t endpoint_id, char (&output)[256]) {
    printf("%s called with 0x%08x\n", __func__, endpoint_id);

    // TODO: behold, a race condition
    if (endpoint_id >= fibre::functions_.size())
        return false;
    fibre::LocalEndpoint* endpoint = fibre::functions_[endpoint_id];
    endpoint->get_as_json(output);
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

/* @brief Initializes Fibre */
void init() {
    static bool initialized = false;
    if (!initialized) {
        fibre::publish_function(&a);
        initialized = true;
    }
}


/*
* @brief Processes a packet originating form a known remote note
* This function returns immediately if all published functions return immediately.
*/
int process_packet(RemoteNode* origin, const uint8_t* buffer, size_t length) {
    if (!origin) {
        LOG_FIBRE("unknown origin");
        return -1;
    }
    LOG_FIBRE("got packet of length %zu: \r\n", length);
    hexdump(buffer, length);
    if (length < 4)
        return -1;

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
        InputPipe* pipe = origin->get_input_pipe(pipe_no);
        if (!pipe) {
            LOG_FIBRE("no pipe %d associated with this source", pipe_no);
            return -1;
        }
        if (chunk_length > length) {
            LOG_FIBRE("chunk longer than packet");
            return -1;
        }
        //InputPipe& pipe = pipes_[pipe_no];
        pipe->process_chunk(buffer, chunk_offset, chunk_length, chunk_crc, close_pipe);
        buffer += chunk_length;
        length -= chunk_length;
    }

    // The packet should be fully consumed now
    if (length)
        return -1;

    return 0;
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
}





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
                if (endpoint_id >= fibre::functions_.size())
                    return ERROR;
                endpoint_ = fibre::functions_[endpoint_id];

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

                endpoint_->open_connection(*this, nullptr);
                //set_stream(nullptr);
                // TODO: make sure the start_connection call invokes set_stream
                state_ = RECEIVING_PAYLOAD;
                return OK;
            }
        case RECEIVING_PAYLOAD:
            LOG_FIBRE("finished receiving payload");
            if (endpoint_)
                endpoint_->decoder_finished(*this);
            set_stream(nullptr);
            return CLOSED;
        default:
            set_stream(nullptr);
            return ERROR;
    }
}

//int asd() {
//incomplete<sizeof(IncomingConnectionDecoder)> a;
//}
static_assert(sizeof(IncomingConnectionDecoder) == RX_BUF_SIZE, "Something is off. Please fix.");

InputPipe* RemoteNode::get_input_pipe(size_t id) {
    // TODO: limit number of concurrent pipes
    auto emplace_result = server_input_pipes_.emplace(id, id);
    InputPipe& input_pipe = emplace_result.first->second;
    if (emplace_result.second) {
        // the pipe was just constructed
        input_pipe.construct_decoder<IncomingConnectionDecoder>();
    }
    return &input_pipe;
}

std::unordered_map<Uuid, RemoteNode> remote_nodes_;
RemoteNode* get_remote_node(Uuid& uuid) {
    // TODO: limit number of concurrent nodes and garbage collect abandonned nodes
    std::pair<const Uuid, RemoteNode>& remote_node = *(remote_nodes_.emplace(uuid, uuid).first);
    return &(remote_node.second);
}

OutputPipe::status_t OutputPipe::process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) {
    size_t chunk = std::min(length, sizeof(buffer_) - buffer_pos_);
    memcpy(buffer_ + buffer_pos_, buffer, chunk);
    buffer_pos_ += chunk;
    if (processed_bytes)
        *processed_bytes += chunk;
    //remote_node->feed_outputs(this);
    return chunk < length ? FULL : OK;
}

}
