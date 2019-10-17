
#include <fibre/input.hpp>
#include <fibre/logging.hpp>

USE_LOG_TOPIC(INPUT);

namespace fibre {

// TODO: replace with fixed size data structure
using fragmented_calls_t = std::unordered_map<call_id_t, incoming_call_t>;
fragmented_calls_t fragmented_calls{};

int start_or_get_call(Context* ctx, call_id_t call_id, incoming_call_t** call) {
    incoming_call_t* dummy;
    call = call ? call : &dummy;
    incoming_call_t new_call = {.decoder = CallDecoder{ctx}};
    auto it = fragmented_calls.emplace(call_id, new_call).first;
    *call = &it->second;
    return 0;
}

// TODO: error handling:
// Should mark this call finished but not deallocate yet. If we deallocate
// and get another fragment, the new fragment is indistinguishable from
// a new call.
int end_call(call_id_t call_id) {
    FIBRE_LOG(D) << "end call " << call_id;
    auto it = fragmented_calls.find(call_id);
    if (it != fragmented_calls.end())
        fragmented_calls.erase(it);
    return 0;
}

int decode_fragment(Context* ctx, StreamSource* source) {
    FIBRE_LOG(D) << "incoming fragment";
    Decoder<call_id_t>* call_id_decoder = alloc_decoder<call_id_t>(ctx);
    stream_copy_result_t status = stream_copy_all(call_id_decoder, source);
    const call_id_t* call_id_ptr = call_id_decoder->get();
    if ((status.dst_status != StreamSink::CLOSED) || !call_id_ptr) {
        dealloc_decoder(call_id_decoder);
        return -1;
    }
    call_id_t call_id = *call_id_ptr;
    dealloc_decoder(call_id_decoder);
    if (status.src_status != StreamSource::OK) {
        return -1;
    }

    Decoder<size_t>* offset_decoder = alloc_decoder<size_t>(ctx);
    status = stream_copy_all(offset_decoder, source);
    const size_t* offset_ptr = offset_decoder->get();
    if ((status.dst_status != StreamSink::CLOSED) || !offset_ptr) {
        dealloc_decoder(offset_decoder);
        return -1;
    }
    size_t offset = *offset_ptr;
    dealloc_decoder(offset_decoder);
    if (status.src_status != StreamSource::OK) {
        return -1;
    }

    incoming_call_t* call;
    start_or_get_call(ctx, call_id, &call);

    FIBRE_LOG(D) << "incoming fragment on stream " << call_id << ", offset " << offset;

    // TODO: should use bufferless copy here
    uint8_t buf[1024];
    bufptr_t bufptr = {.ptr = buf, .length = sizeof(buf)};
    if (source->get_all_bytes(bufptr) != StreamSource::CLOSED) {
        return -1;
    }
    FIBRE_LOG(D) << "got " << (sizeof(buf) - bufptr.length) << " bytes from the source";
    cbufptr_t cbufptr = {.ptr = buf, .length = (sizeof(buf) - bufptr.length)};
    call->fragment_sink.process_chunk(cbufptr, offset);
    FIBRE_LOG(D) << "processed chunk";

    stream_copy_result_t copy_result = stream_copy_all(&call->decoder, &call->fragment_sink);
    if (copy_result.src_status == StreamSource::ERROR) {
        FIBRE_LOG(E) << "defragmenter failed";
        return -1;
    }
    if ((copy_result.dst_status == StreamSink::ERROR) || (copy_result.dst_status == StreamSink::CLOSED)) {
        end_call(call_id);
    }

    return 0;
}

int encode_fragment(outgoing_call_t call, StreamSink* sink) {
    Encoder<call_id_t>* call_id_encoder = alloc_encoder<call_id_t>(call.ctx);
    call_id_encoder->set(&call.uuid);
    stream_copy_result_t status = stream_copy_all(sink, call_id_encoder);
    dealloc_encoder(call_id_encoder);
    call_id_encoder = nullptr;
    if ((status.src_status != StreamSource::CLOSED) || (status.dst_status != StreamSink::OK)) {
        return -1;
    }

    stream_copy_all(&call.fragment_source, &call.encoder);

    // TODO: should we send along the length of the chunk?
    size_t offset;
    cbufptr_t chunk = {.ptr = nullptr, .length = 1024}; // TODO: pass in max length based on output MTU
    call.fragment_source.get_chunk(chunk, &offset);

    FIBRE_LOG(D) << "encode fragment: stream " << call.uuid << ", chunk " << offset << " length " << chunk.length;

    Encoder<size_t>* offset_encoder = alloc_encoder<size_t>(call.ctx);
    offset_encoder->set(&offset);
    status = stream_copy_all(sink, offset_encoder);
    dealloc_encoder(offset_encoder);
    offset_encoder = nullptr;
    if ((status.src_status != StreamSource::CLOSED) || (status.dst_status != StreamSink::OK)) {
        return -1;
    }

    FIBRE_LOG(D) << "add fragment content: " << chunk.length << " bytes";
    sink->process_all_bytes(chunk); // TODO: error handling
    FIBRE_LOG(D) << "added fragment content";
    return 0;
}

}

/*
void InputPipe::process_chunk(const uint8_t* buffer, size_t offset, size_t length, uint16_t crc, bool packet_break) {
    if (offset > pos_) {
        FIBRE_LOG(W) << "disjoint chunk reassembly not implemented";
        // TODO: implement disjoint chunk reassembly
        return;
    }
    if ((offset + length < pos_) || ((offset + length == pos_) && at_packet_break)) {
        FIBRE_LOG(W) << "duplicate data received";
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
        FIBRE_LOG(W) << "received dangling chunk: expected CRC " << as_hex(crc_) << " but got " << as_hex(crc);
        return;
    }
    FIBRE_LOG(D) << "input pipe " << id_ << ": process " << as_hex(length) << " bytes...";

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
                    FIBRE_LOG(W) << "input handler for pipe " << id_ << " terminated abnormally: status " << status << ", processed " << processed_bytes << " bytes, should have processed " << length << " bytes";
                }
                set_handler(nullptr);
            }
        } else {
            FIBRE_LOG(W) << "the pipe " << id_ << " has no input handler - maybe the input handler terminated abnormally";
        }
    } else {
        FIBRE_LOG(D) << "received standalone packet break";
    }

    pos_ = offset + length;
    crc_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc_, buffer, length);
    at_packet_break = packet_break;
    if (packet_break) {
        if (input_handler_) {
            set_handler(nullptr);
            FIBRE_LOG(W) << "packet break on pipe " << id_ << " but the input handler was not done";
        }
        FIBRE_LOG(D) << "received packet break";
    }

    //// TODO: ACK/NACK received bytes
}

InputChannelDecoder::status_t InputChannelDecoder::process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) {
    FIBRE_LOG(D) << "received " << length << " bytes from " << remote_node_->get_uuid();
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
                FIBRE_LOG(D) << "received chunk header: pipe " << get_pipe_no() << ", offset " << as_hex(get_chunk_offset()) << ", (length << 1) | packet_break " << as_hex(get_chunk_length()) << ", crc " << as_hex(get_chunk_crc());
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
                FIBRE_LOG(W) << "no pipe " << pipe_id << " associated with this source";
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
                FIBRE_LOG(D) << "finished receiving header: endpoint " << endpoint_id << ", hash " << as_hex(endpoint_hash);
                
                // TODO: behold, a race condition
                if (endpoint_id >= global_state.functions_.size())
                    return ERROR;
                endpoint_ = global_state.functions_[endpoint_id];

                if (!endpoint_) {
                    FIBRE_LOG(W) << "no endpoint at " << endpoint_id;
                    return ERROR;
                }

                // Verify endpoint hash. The expected hash value depends on the selected endpoint.
                // For endpoint 0 this is just the protocol version, for all other endpoints it's a
                // CRC over the entire JSON descriptor tree (this may change in future versions).
                uint16_t expected_hash = endpoint_->get_hash();
                if (expected_hash != endpoint_hash) {
                    FIBRE_LOG(W) << "hash mismatch for endpoint " << endpoint_id << ": expected " << as_hex(expected_hash) << ", got " << as_hex(endpoint_hash);
                    return ERROR;
                }
                FIBRE_LOG(D) << "hash ok for endpoint " << endpoint_id;

                endpoint_->open_connection(*this);
                //set_stream(nullptr);
                // TODO: make sure the start_connection call invokes set_stream
                state_ = RECEIVING_PAYLOAD;
                return OK;
            }
        case RECEIVING_PAYLOAD:
            FIBRE_LOG(D) << "finished receiving payload";
            if (endpoint_)
                endpoint_->decoder_finished(*this, output_pipe_);
            set_stream(nullptr);
            return CLOSED;
        default:
            set_stream(nullptr);
            return ERROR;
    }
}

*/
