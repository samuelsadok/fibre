
#include <fibre/fibre.hpp>

using namespace fibre;

void InputPipe::process_chunk(const uint8_t* buffer, size_t offset, size_t length, uint16_t crc, bool packet_break) {
    if (offset > pos_) {
        LOG_FIBRE_W(INPUT, "disjoint chunk reassembly not implemented");
        // TODO: implement disjoint chunk reassembly
        return;
    }
    if ((offset + length < pos_) || ((offset + length == pos_) && at_packet_break)) {
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
    LOG_FIBRE(INPUT, "input pipe ", id_, ": process ", as_hex(length), " bytes...");

    // Open connection on demand
    if (!input_handler_ && at_packet_break) {
        construct_decoder<IncomingConnectionDecoder>(*output_pipe_);
    }

    if (length) {
        if (input_handler_) {
            size_t processed_bytes = 0;
            StreamSink::status_t status = input_handler_->process_bytes(buffer, length, &processed_bytes);

            if (status != StreamSink::OK || processed_bytes != length) {
                if (status != StreamSink::CLOSED || processed_bytes != length) {
                    LOG_FIBRE_W(INPUT, "input handler for pipe ", id_, " terminated abnormally: status ", status, ", processed ", processed_bytes, " bytes, should have processed ", length, " bytes");
                }
                set_handler(nullptr);
            }
        } else {
            LOG_FIBRE_W(INPUT, "the pipe ", id_, " has no input handler - maybe the input handler terminated abnormally");
        }
    } else {
        LOG_FIBRE(INPUT, "received standalone packet break");
    }

    pos_ = offset + length;
    crc_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc_, buffer, length);
    at_packet_break = packet_break;
    if (packet_break) {
        if (input_handler_) {
            set_handler(nullptr);
            LOG_FIBRE_W(INPUT, "packet break on pipe ", id_, " but the input handler was not done");
        }
        LOG_FIBRE(INPUT, "received packet break");
    }

    //// TODO: ACK/NACK received bytes
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
                LOG_FIBRE(INPUT, "received chunk header: pipe ", get_pipe_no(), ", offset ", as_hex(get_chunk_offset()), ", (length << 1) | packet_break ", as_hex(get_chunk_length()), ", crc ", as_hex(get_chunk_crc()));
                in_header = false;
            }
        } else {
            uint16_t pipe_id = get_pipe_no();
            uint16_t& chunk_offset = get_chunk_offset();
            uint16_t& chunk_length = get_chunk_length();
            uint16_t& chunk_crc = get_chunk_crc();

            size_t actual_length = std::min(static_cast<size_t>(chunk_length >> 1), length);
            bool packet_break = ((actual_length << 1) | 1) == chunk_length;

            InputPipe* input_pipe;
            OutputPipe* output_pipe;
            std::tie(input_pipe, output_pipe)
                = remote_node_->get_pipe_pair(pipe_id, !(pipe_id & 1));
            if (!input_pipe) {
                LOG_FIBRE_W(INPUT, "no pipe ", pipe_id, " associated with this source");
                reset();
                continue;
            }

            input_pipe->process_chunk(buffer, get_chunk_offset(), actual_length, get_chunk_crc(), packet_break);

            chunk_crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(chunk_crc, buffer, actual_length);
            buffer += actual_length;
            length -= actual_length;
            chunk_offset += actual_length;
            chunk_length -= actual_length << 1;

            if (processed_bytes)
                *processed_bytes += actual_length;

            if (!(chunk_length >> 1)) {
                reset();
            }
        }
    }
    return OK;
}

size_t InputChannelDecoder::get_min_useful_bytes() const {
    if (in_header) {
        return header_decoder_.get_min_useful_bytes();
    } else {
        return 1; // non-trivial to get a better lower bound here
    }
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
