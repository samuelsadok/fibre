
/* Includes ------------------------------------------------------------------*/

#include <fibre/fibre.hpp>

#include <memory>
#include <stdlib.h>
#include <random>

namespace fibre {
    global_state_t global_state;
}

#if 0

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
#endif

namespace fibre {

void publish_function(LocalEndpoint* function) {
    global_state.functions_.push_back(function);
}

void publish_ref_type(FibreRefType* type) {
    global_state.ref_types_.push_back(type);
}

/* Built-in published functions ---------------------------------------------*/

bool get_function_json(uint32_t endpoint_id, const char ** output, size_t* length) {
    LOG_FIBRE(GENERAL, "fetching JSON of function ", as_hex(endpoint_id));

    if (output) *output = nullptr;
    if (length) *length = 0;

    // TODO: behold, a race condition
    if (endpoint_id >= global_state.functions_.size()) {
        LOG_FIBRE_W(GENERAL, "endpoint_id out of range: ", endpoint_id, " >= ", global_state.functions_.size());
        return false;
    }
    fibre::LocalEndpoint* endpoint = global_state.functions_[endpoint_id];
    endpoint->get_as_json(output, length);
    return true;
}

constexpr auto get_function_json__function_properties = 
    make_function_props("get_function_json")
    .with_inputs("endpoint_id")
    .with_outputs("json");

fibre::LocalFunctionEndpoint<
    decltype(get_function_json), get_function_json,
    decltype(get_function_json__function_properties), get_function_json__function_properties> get_function_json__endpoint;


//constexpr const char get_function_json_function_name[] = "get_function_json";
//constexpr const std::tuple<const char (&)[12]> get_function_json_input_names("endpoint_id");
//constexpr const std::tuple<const char (&)[5]> get_function_json_output_names("json");
//using get_function_json_properties = StaticFunctionProperties<
//    decltype(get_function_json_function_name),
//    std::remove_const_t<decltype(get_function_json_input_names)>,
//    std::remove_const_t<decltype(get_function_json_output_names)>
//    >::WithStaticNames<get_function_json_function_name, get_function_json_input_names, get_function_json_output_names>;

//auto a = FunctionStuff<std::tuple<uint32_t>, std::tuple<char[256]>, decltype(get_function_json__function_properties), get_function_json__function_properties>
//        //::template WithStaticNames<get_function_json_names>
//        ::template WithStaticFuncPtr2<get_function_json>();

/**
 * @brief Runs one scheduler iteration for all remote nodes.
 */
void schedule_all() {
    // TODO: make thread-safe
    LOG_FIBRE(GENERAL, "running global scheduler");
    for (std::pair<const Uuid, RemoteNode>& kv : global_state.remote_nodes_) {
        kv.second.schedule();
    }
}

void scheduler_loop() {
    LOG_FIBRE(GENERAL, "global scheduler thread started");
    for (;;) {
        global_state.output_pipe_ready.wait();
        //global_state.output_channel_ready.wait();
        schedule_all();
    }
}

Logger logger;
Logger* get_logger() { return &logger; }

/* @brief Initializes Fibre */
void init() {
    if (global_state.initialized) {
        LOG_FIBRE_W(GENERAL, "already initialized");
        return;
    }

    // TODO: global_state should not be constructed statically
    fibre::publish_function(&get_function_json__endpoint);
    //fibre::publish_function(&a);

    std::random_device rd;
    std::uniform_int_distribution<uint8_t> dist;
    uint8_t buffer[16] = { 0 };
    for (size_t i = 0; i < sizeof(buffer); ++i)
        buffer[i] = dist(rd);
    global_state.own_uuid = Uuid(buffer);

#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_GLOBAL_THREAD
    global_state.scheduler_thread = std::thread(scheduler_loop);
    LOG_FIBRE(GENERAL, "launched scheduler thread");
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


RemoteNode* get_remote_node(Uuid uuid) {
    // TODO: limit number of concurrent nodes and garbage collect abandonned nodes
    std::pair<const Uuid, RemoteNode>& remote_node = *(global_state.remote_nodes_.emplace(uuid, uuid).first);
    return &(remote_node.second);
}

} // namespace fibre
