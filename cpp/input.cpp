
#include <fibre/fibre.hpp>

using namespace fibre;

void InputPipe::process_chunk(const uint8_t* buffer, size_t offset, size_t length, uint16_t crc, bool close_pipe) {
    if (offset > pos_) {
        LOG_FIBRE_W(INPUT, "disjoint chunk reassembly not implemented");
        // TODO: implement disjoint chunk reassembly
        return;
    }
    if (offset + length <= pos_) {
        LOG_FIBRE_W(INPUT, "duplicate data received");
        return;
    }
    // dump the beginning of the chunk if it's already known
    if (offset < pos_) {
        size_t diff = pos_ - offset;
        crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc, buffer, diff);
        buffer += diff;
        offset += diff;
        length -= diff;
    }
    if (crc != crc_) {
        LOG_FIBRE_W(INPUT, "received dangling chunk: expected CRC ", as_hex(crc_), " but got ", as_hex(crc));
        return;
    }
    input_handler->process_bytes(buffer, length, nullptr /* TODO: why? */);
    pos_ = offset + length;
    // TODO: acknowledge received bytes
    if (close_pipe) {
        close();
    }
}

InputChannelDecoder::status_t InputChannelDecoder::process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) {
    LOG_FIBRE(INPUT, "received ", length, " bytes from ", remote_node_->get_uuid());
    while (length) {
        size_t chunk = 0;
        if (in_header) {
            status_t status = header_decoder_.process_bytes(buffer, length, &chunk);

            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;

            // finished receiving chunk header
            if (status == CLOSED) {
                LOG_FIBRE(INPUT, "received chunk header: pipe ", get_pipe_no(), ", offset ", as_hex(get_chunk_offset()), ", length ", as_hex(get_chunk_length()), ", crc ", as_hex(get_chunk_crc()));
                in_header = false;
                bool is_new = false;
                uint16_t pipe_no = get_pipe_no();
                input_pipe_ = remote_node_->get_input_pipe(pipe_no, &is_new);
                if (!input_pipe_) {
                    LOG_FIBRE_W(INPUT, "no pipe ", pipe_no, " associated with this source", pipe_no);
                    //reset();
                    continue;
                }
                if (is_new) {
                    OutputPipe* output_pipe = remote_node_->get_output_pipe(pipe_no & 0x8000);
                    input_pipe_->construct_decoder<IncomingConnectionDecoder>(*output_pipe);
                }
            }
        } else {
            uint16_t& chunk_offset = get_chunk_offset();
            uint16_t& chunk_length = get_chunk_length();
            uint16_t& chunk_crc = get_chunk_crc();

            size_t actual_length = std::min(static_cast<size_t>(chunk_length), length);
            if (input_pipe_)
                input_pipe_->process_chunk(buffer, get_chunk_offset(), actual_length, get_chunk_crc(), false);
            //status_t status = input_pipe_.process_bytes(buffer, std::min(length, remaining_payload_bytes_), &chunk);

            chunk_crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(chunk_crc, buffer, actual_length);
            buffer += actual_length;
            length -= actual_length;
            chunk_offset += actual_length;
            chunk_length -= actual_length;

            if (processed_bytes)
                *processed_bytes += actual_length;

            if (!chunk_length) {
                reset();
            }
        }
    }
    return OK;
}


IncomingConnectionDecoder::status_t IncomingConnectionDecoder::advance_state() {
    switch (state_) {
        case RECEIVING_HEADER: {
                HeaderDecoderChain *header_decoder = get_stream<HeaderDecoderChain>();
                uint16_t endpoint_id = header_decoder->get_stream<0>().get_value();
                uint16_t endpoint_hash = header_decoder->get_stream<1>().get_value();
                LOG_FIBRE(INPUT, "finished receiving header: endpoint ", endpoint_id, ", hash ", as_hex(endpoint_hash));
                
                // TODO: behold, a race condition
                if (endpoint_id >= global_state.functions_.size())
                    return ERROR;
                endpoint_ = global_state.functions_[endpoint_id];

                if (!endpoint_) {
                    LOG_FIBRE_W(INPUT, "no endpoint at ", endpoint_id);
                    return ERROR;
                }

                // Verify endpoint hash. The expected hash value depends on the selected endpoint.
                // For endpoint 0 this is just the protocol version, for all other endpoints it's a
                // CRC over the entire JSON descriptor tree (this may change in future versions).
                uint16_t expected_hash = endpoint_->get_hash();
                if (expected_hash != endpoint_hash) {
                    LOG_FIBRE_W(INPUT, "hash mismatch for endpoint ", endpoint_id, ": expected ", as_hex(expected_hash), ", got ", as_hex(endpoint_hash));
                    return ERROR;
                }
                LOG_FIBRE(INPUT, "hash ok for endpoint ", endpoint_id);

                endpoint_->open_connection(*this);
                //set_stream(nullptr);
                // TODO: make sure the start_connection call invokes set_stream
                state_ = RECEIVING_PAYLOAD;
                return OK;
            }
        case RECEIVING_PAYLOAD:
            LOG_FIBRE(INPUT, "finished receiving payload");
            if (endpoint_)
                endpoint_->decoder_finished(*this, output_pipe_);
            set_stream(nullptr);
            return CLOSED;
        default:
            set_stream(nullptr);
            return ERROR;
    }
}
