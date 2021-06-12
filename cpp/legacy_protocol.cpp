

#include "legacy_protocol.hpp"

#include "protocol.hpp"
#include "crc.hpp"
#include "print_utils.hpp"
#include <fibre/async_stream.hpp>
#include <memory>
#include <stdlib.h>
#include <algorithm>

using namespace fibre;


/* PacketWrapper -------------------------------------------------------------*/

void PacketWrapper::start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult0> completer) {
    if (handle) {
        *handle = reinterpret_cast<TransferHandle>(this);
    }

    if (state_ != kStateIdle) {
        completer.invoke({kStreamError, buffer.begin()});
    }

    // TODO: support buffer size >= 128
    if (buffer.size() >= 128) {
        completer.invoke({kStreamError, buffer.begin()});
    }

    completer_ = completer;

    header_buf_[0] = CANONICAL_PREFIX;
    header_buf_[1] = static_cast<uint8_t>(buffer.size());
    header_buf_[2] = calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, header_buf_, 2);
    
    payload_buf_ = buffer;

    uint16_t crc16 = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, buffer.begin(), buffer.size());
    trailer_buf_[0] = (uint8_t)((crc16 >> 8) & 0xff),
    trailer_buf_[1] = (uint8_t)((crc16 >> 0) & 0xff);

    state_ = kStateSendingHeader;
    expected_tx_end_ = header_buf_ + 3;
    tx_channel_->start_write(header_buf_, &inner_transfer_handle_, MEMBER_CB(this, complete));
}

void PacketWrapper::cancel_write(TransferHandle transfer_handle) {
    state_ = kStateCancelling;
    tx_channel_->cancel_write(inner_transfer_handle_);
}

void PacketWrapper::complete(WriteResult0 result) {
    if (state_ == kStateCancelling) {
        state_ = kStateIdle;
        completer_.invoke_and_clear({kStreamCancelled, payload_buf_.begin()});
        return;
    }

    if (result.status != kStreamOk) {
        state_ = kStateIdle;
        completer_.invoke_and_clear({result.status, payload_buf_.begin()});
        return;
    }

    if (result.end < expected_tx_end_) {
        tx_channel_->start_write({result.end, expected_tx_end_}, &inner_transfer_handle_, MEMBER_CB(this, complete));
        return;
    }

    if (state_ == kStateSendingHeader) {
        state_ = kStateSendingPayload;
        expected_tx_end_ = payload_buf_.end();
        tx_channel_->start_write(payload_buf_, &inner_transfer_handle_, MEMBER_CB(this, complete));

    } else if (state_ == kStateSendingPayload) {
        state_ = kStateSendingTrailer;
        expected_tx_end_ = trailer_buf_ + 2;
        tx_channel_->start_write(trailer_buf_, &inner_transfer_handle_, MEMBER_CB(this, complete));

    } else if (state_ == kStateSendingTrailer) {
        state_ = kStateIdle;
        completer_.invoke_and_clear({kStreamOk, payload_buf_.end()});
    }
}


/* PacketUnwrapper -----------------------------------------------------------*/

void PacketUnwrapper::start_read(bufptr_t buffer, TransferHandle* handle, Callback<void, ReadResult> completer) {
    if (handle) {
        *handle = reinterpret_cast<TransferHandle>(this);
    }

    if (state_ != kStateIdle) {
        completer.invoke({kStreamError, buffer.begin()});
    }

    completer_ = completer;
    payload_buf_ = buffer;

    state_ = kStateReceivingHeader;
    expected_rx_end_ = rx_buf_ + 3;
    rx_channel_->start_read({rx_buf_, expected_rx_end_}, &inner_transfer_handle_, MEMBER_CB(this, complete));
}

void PacketUnwrapper::cancel_read(TransferHandle transfer_handle) {
    state_ = kStateCancelling;
    rx_channel_->cancel_read(inner_transfer_handle_);
}

void PacketUnwrapper::complete(ReadResult result) {
    // All code paths in this function must end with either of these two:
    //   - rx_channel_->start_read() to bounce back control to the underlying stream
    //   - safe_complete() to return control to the client

    if (state_ == kStateCancelling) {
        state_ = kStateIdle;
        completer_.invoke_and_clear({kStreamCancelled, payload_buf_.begin()});
        return;
    }

    if (result.status != kStreamOk) {
        state_ = kStateIdle;
        completer_.invoke_and_clear({result.status, payload_buf_.begin()});
        return;
    }

    if (result.end < expected_rx_end_) {
        rx_channel_->start_read({result.end, expected_rx_end_}, &inner_transfer_handle_, MEMBER_CB(this, complete));
        return;
    }

    if (state_ == kStateReceivingHeader) {
        size_t n_discard;

        // Process header
        if (rx_buf_[0] != CANONICAL_PREFIX) {
            n_discard = 1;
        } else if ((rx_buf_[1] & 0x80)) {
            n_discard = 2; // TODO: support packets larger than 128 bytes
        } else if (calc_crc8<CANONICAL_CRC8_POLYNOMIAL>(CANONICAL_CRC8_INIT, rx_buf_, 3)) {
            n_discard = 3;
        } else {
            state_ = kStateReceivingPayload;
            payload_length_ = std::min(payload_buf_.size(), (size_t)rx_buf_[1]);
            expected_rx_end_ = payload_buf_.begin() + payload_length_;
            rx_channel_->start_read(payload_buf_.take(payload_length_), &inner_transfer_handle_, MEMBER_CB(this, complete));
            return;
        }

        // Header was bad: discard the bad header bytes and receive more
        memmove(rx_buf_, rx_buf_ + n_discard, sizeof(rx_buf_) - n_discard);
        rx_channel_->start_read(bufptr_t{rx_buf_}.skip(3 - n_discard), &inner_transfer_handle_, MEMBER_CB(this, complete));

    } else if (state_ == kStateReceivingPayload) {
        expected_rx_end_ = rx_buf_ + 2;
        state_ = kStateReceivingTrailer;
        rx_channel_->start_read({rx_buf_, expected_rx_end_}, &inner_transfer_handle_, MEMBER_CB(this, complete));

    } else if (state_ == kStateReceivingTrailer) {
        uint16_t crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(CANONICAL_CRC16_INIT, payload_buf_.begin(), payload_length_);
        crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc, rx_buf_, 2);

        if (!crc) {
            state_ = kStateIdle;
            completer_.invoke_and_clear({kStreamOk, payload_buf_.begin() + payload_length_});
        } else {
            state_ = kStateReceivingHeader;
            expected_rx_end_ = rx_buf_ + 3;
            rx_channel_->start_read({rx_buf_, expected_rx_end_}, &inner_transfer_handle_, MEMBER_CB(this, complete));
        }
    }
}


/* LegacyProtocolPacketBased -------------------------------------------------*/

#if FIBRE_ENABLE_CLIENT

Socket* LegacyProtocolPacketBased::start_call(uint16_t ep_num, uint16_t json_crc, std::vector<uint16_t> in_arg_ep_nums, std::vector<uint16_t> out_arg_ep_nums, Socket* caller) {
    Call* call = new Call{}; // deleted in write() or on_write_done()
    call->parent_ = this;
    call->ep_num_ = ep_num;
    call->json_crc_ = json_crc;
    call->caller_ = caller;
    call->in_arg_ep_nums_ = in_arg_ep_nums;
    call->out_arg_ep_nums_ = out_arg_ep_nums;
    return call;
}

WriteResult LegacyProtocolPacketBased::Call::write(WriteArgs args) {
    while (args.buf.n_chunks()) {
        Chunk chunk = args.buf.front();
        args.buf = args.buf.skip_chunks(1);

        if (chunk.is_buf() && chunk.layer() == 0) {
            for (auto b : chunk.buf()) {
                last_arg_.push_back(b);
            }
        } else if (chunk.is_frame_boundary() && chunk.layer() == 0) {
            in_args_.push_back(last_arg_);
            last_arg_ = {};
        } else {
            error_ = true;
        }
    }

    // Dispatch legacy-style endpoint operations for each argument
    if (args.status == kFibreClosed && !error_) {
        if (in_args_.size() != in_arg_ep_nums_.size()) {
            error_ = true;
        }

        for (size_t i = 0; i < in_arg_ep_nums_.size(); ++i) {
            if (in_arg_ep_nums_[i] != ep_num_) {
                ops_.push_back(parent_->start_endpoint_operation(in_arg_ep_nums_[i], json_crc_, in_args_[i], {}, MEMBER_CB(this, on_ep_operation_done)));
            }
        }

        std::vector<uint8_t> buf;
        buf.resize(512);
        out_args_ = std::vector<std::vector<uint8_t>>{out_arg_ep_nums_.size(), buf};

        ops_.push_back(parent_->start_endpoint_operation(ep_num_, json_crc_,
            in_arg_ep_nums_.size() == 1 && in_arg_ep_nums_[0] == ep_num_ ? cbufptr_t{in_args_[0]} : cbufptr_t{},
            out_arg_ep_nums_.size() == 1 && out_arg_ep_nums_[0] == ep_num_ ? bufptr_t{out_args_[0]} : bufptr_t{},
            MEMBER_CB(this, on_ep_operation_done)));

        for (size_t i = 0; i < out_arg_ep_nums_.size(); ++i) {
            if (out_arg_ep_nums_[i] != ep_num_) {
                ops_.push_back(parent_->start_endpoint_operation(out_arg_ep_nums_[i], json_crc_, {}, out_args_[i], MEMBER_CB(this, on_ep_operation_done)));
            }
        }
    }

    return {args.status, args.buf.begin()};
}

void LegacyProtocolPacketBased::Call::on_ep_operation_done(EndpointOperationResult result) {
    if (!ops_.size() || ops_.front() != result.op) {
        error_ = true;
        return;
    }
    
    if (ops_.size() <= out_arg_ep_nums_.size()) {
        // special handling for endpoint 0
        bool restart = ep_num_ == 0 && (result.rx_end != &*out_args_[n_out_args].end() - 512) && in_args_.size() == 1 && in_args_[0].size() == 4;

        out_args_[n_out_args].erase(out_args_[n_out_args].begin() + (result.rx_end - out_args_[n_out_args].data()), out_args_[n_out_args].end());

        if (restart) {
            write_le<uint32_t>(out_args_[n_out_args].size(), in_args_[0].data());
            out_args_[n_out_args].resize(out_args_[n_out_args].size() + 512);
            ops_[0] = parent_->start_endpoint_operation(in_arg_ep_nums_[0], json_crc_, in_args_[0], bufptr_t{out_args_[0]}.skip(out_args_[0].size() - 512), MEMBER_CB(this, on_ep_operation_done));
            return;
        }

        n_out_args++;
    }

    ops_.erase(ops_.begin());

    if (!ops_.size()) {
        for (size_t i = 0; i < out_args_.size(); ++i) {
            chunks_.push_back(Chunk(0, out_args_[i]));
            chunks_.push_back(Chunk::frame_boundary(0));
        }
        chunk_pos = BufChain{&*chunks_.begin(), &*chunks_.end()}.begin();

        // Pass output args to caller
        for (;;) {
            WriteResult result = caller_->write({{chunk_pos, &*chunks_.end()}, kFibreClosed});
            if (result.is_busy()) {
                break;
            }
            chunk_pos = result.end;
            if (chunk_pos == CBufIt{&*chunks_.end()} && result.status != kFibreOk) {
                delete this; // close call
                return;
            }
        }
    }
}

WriteArgs LegacyProtocolPacketBased::Call::on_write_done(WriteResult result) {
    chunk_pos = result.end;
    BufChain next = {chunk_pos, &*chunks_.end()};
    if (result.status != kFibreOk) {
        delete this; // close call
        return {{}, result.status};
    } else {
        return {next, kFibreClosed};
    }
}


/**
 * @brief Starts a remote endpoint operation.
 * 
 * @param endpoint_id: The endpoint ID to invoke the operation on.
 * @param tx_buf: The tx_buf to write to the endpoint. Must remain valid until
 *        the completer is invoked.
 * @param rx_length: The desired number of bytes to read from the endpoint. The
 *        actual returned buffer may be smaller.
 * @param completer: The completer that will be notified once the operation
 *        completes (whether successful or not).
 *        The buffer given to the completer is only valid if the status is
 *        kStreamOk and until the completer returns.
 * @param handle: The variable pointed to by this argument is set to a handle
 *        that can be passed to cancel_endpoint_operation() to cancel the
 *        ongoing operation. If the completer is invoked directly from within
 *        this function then the handle is not set later than invoking the
 *        completer.
 */
EndpointOperationHandle LegacyProtocolPacketBased::start_endpoint_operation(uint16_t endpoint_id, uint16_t json_crc, cbufptr_t tx_buf, bufptr_t rx_buf, Callback<void, EndpointOperationResult> callback) {
    outbound_seq_no_ = ((outbound_seq_no_ + 1) & 0x7fff);

    EndpointOperation op = {
        .seqno = (uint16_t)(outbound_seq_no_ | 0x0080), // FIXME: we hardwire one bit of the seq-no to 1 to avoid conflicts with the ODrive ASCII protocol
        .endpoint_id = endpoint_id,
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .callback = callback
    };

    if (tx_handle_) {
        F_LOG_D(domain_->ctx->logger, "Endpoint operation already in progress. Enqueuing this one.");

        // A TX operation is already in progress. Enqueue this one.
        pending_operations_.push_back(op);
    } else {
        start_endpoint_operation(op);
    }

    return op.seqno | 0xffff0000;
}

void LegacyProtocolPacketBased::start_endpoint_operation(EndpointOperation op) {
    write_le<uint16_t>(op.seqno, tx_buf_);
    write_le<uint16_t>(op.endpoint_id | 0x8000, tx_buf_ + 2);
    write_le<uint16_t>(op.rx_buf.size(), tx_buf_ + 4);

    size_t mtu = std::min(sizeof(tx_buf_), tx_mtu_);
    size_t n_payload = std::min(std::max(mtu, (size_t)8) - 8, op.tx_buf.size());

    memcpy(tx_buf_ + 6, op.tx_buf.begin(), n_payload);

    uint16_t trailer = (op.endpoint_id & 0x7fff) == 0 ?
                       PROTOCOL_VERSION : client_.json_crc_;

    write_le<uint16_t>(trailer, tx_buf_ + 6 + n_payload);

    expected_acks_[op.seqno] = op;
    transmitting_op_ = op.seqno | 0xffff0000;
    tx_channel_->start_write(cbufptr_t{tx_buf_}.take(8 + n_payload), &tx_handle_, MEMBER_CB(this, on_write_finished));
}

/*
void LegacyProtocolPacketBased::cancel_endpoint_operation(EndpointOperationHandle handle) {
    if (!handle) {
        return;
    }

    uint16_t seqno = static_cast<uint16_t>(handle & 0xffff);

    Callback<void, EndpointOperationResult> callback;
    const uint8_t* tx_end = nullptr;
    uint8_t* rx_end = nullptr;

    auto it0 = std::find_if(pending_operations_.begin(), pending_operations_.end(), [&](EndpointOperation& op) {
        return op.seqno == seqno;
    });

    if (it0 != pending_operations_.end()) {
        callback = it0->callback;
        tx_end = it0->tx_buf.begin();
        rx_end = it0->rx_buf.begin();
        pending_operations_.erase(it0);
    }

    auto it1 = expected_acks_.find(seqno);

    if (it1 != expected_acks_.end()) {
        callback = it1->second.callback;
        tx_end = it1->second.tx_buf.begin();
        rx_end = it1->second.rx_buf.begin();
        expected_acks_.erase(it1);
    }

    if (transmitting_op_ == handle) {
        // Cancel the TX task because it belongs to the endpoint operation that
        // is being cancelled.
        tx_channel_->cancel_write(tx_handle_);
    } else {
        // Either we're waiting for an ack on this operation or it has not yet
        // been sent. In both cases we can just complete immediately.
        callback.invoke_and_clear({kStreamCancelled, tx_end, rx_end});
    }
}*/

#endif

void LegacyProtocolPacketBased::on_write_finished(WriteResult0 result) {
    tx_handle_ = 0;

    if (rx_status_ != kStreamOk) {
        on_rx_tx_closed(rx_status_);
        return;
    }

#if FIBRE_ENABLE_CLIENT
    if (transmitting_op_) {
        uint16_t seqno = transmitting_op_ & 0xffff;
        EndpointOperationHandle handle = transmitting_op_;
        transmitting_op_ = 0;

        auto it = expected_acks_.find(seqno);

        size_t n_sent = std::max((size_t)(result.end - tx_buf_), (size_t)8) - 8;
        it->second.tx_buf = it->second.tx_buf.skip(n_sent);
        it->second.tx_done = true;

        if (it->second.rx_done) {
            // It's possible that the RX operation completes before the TX operation
            auto op = it->second;
            expected_acks_.erase(it);
            op.callback.invoke_and_clear({handle, kStreamOk, op.tx_buf.begin(), op.rx_buf.begin()});
        } else if (result.status != kStreamOk) {
            // If the TX task was a remote endpoint operation but didn't succeed
            // we terminate that operation
            auto op = it->second;
            expected_acks_.erase(it);
            op.callback.invoke_and_clear({handle, result.status, result.end, op.rx_buf.begin()});
        }

        if (transmitting_op_) {
            return;
        }
    }
#endif

    // TODO: should we prioritize the server or client side here?

#if FIBRE_ENABLE_SERVER
    if (rx_end_) {
        // There is a write operation pending from the server side (i.e. an ack
        // for a local endpoint operation).
        uint8_t* rx_end = rx_end_;
        rx_end_ = nullptr;
        on_read_finished({kStreamOk, rx_end});
#if FIBRE_ENABLE_CLIENT
        if (transmitting_op_) {
            return;
        }
#endif
    }
#endif

#if FIBRE_ENABLE_CLIENT
    if (pending_operations_.size() > 0) {
        // There is a write operation pending from the client side (i.e. an
        // outgoing remote endpoint operation).
        EndpointOperation op = pending_operations_[0];
        pending_operations_.erase(pending_operations_.begin());
        start_endpoint_operation(op);
        if (transmitting_op_) {
            return;
        }
    }
#endif
}

void LegacyProtocolPacketBased::on_read_finished(ReadResult result) {
    TransferHandle dummy;

    if (result.status == kStreamClosed) {
        F_LOG_D(domain_->ctx->logger, "RX stream closed.");
        on_rx_closed(kStreamClosed);
        return;
    } else if (result.status == kStreamCancelled) {
        F_LOG_E(domain_->ctx->logger, "RX operation cancelled.");
        on_rx_closed(kStreamCancelled);
        return;
    } else if (result.status != kStreamOk) {
        F_LOG_E(domain_->ctx->logger, "RX error. Not restarting.");
        // TODO: we should distinguish between permanent and temporary errors.
        // If we try to restart after a permanent error we might end up in a
        // busy loop.
        on_rx_closed(kStreamError);
        return;
    }

    cbufptr_t rx_buf = cbufptr_t{rx_buf_, result.end};
    //F_LOG_D(domain_->ctx->logger, "got packet of length " << (result.end - rx_buf_) /*<< ": " << as_hex(rx_buf)*/);

    // TODO: think about some kind of ordering guarantees
    // currently the seq_no is just used to associate a response with a request
    std::optional<uint16_t> seq_no = read_le<uint16_t>(&rx_buf);

    if (!seq_no.has_value()) {
        F_LOG_E(domain_->ctx->logger, "packet too short");

    } else if (*seq_no & 0x8000) {

#if FIBRE_ENABLE_CLIENT
        
        auto it = expected_acks_.find(*seq_no & 0x7fff);

        if (it == expected_acks_.end()) {
            F_LOG_E(domain_->ctx->logger, "received unexpected ACK: " << (*seq_no & 0x7fff));
        } else {
            size_t n_copy = std::min((size_t)(result.end - rx_buf.begin()), it->second.rx_buf.size());
            memcpy(it->second.rx_buf.begin(), rx_buf.begin(), n_copy);
            it->second.rx_buf = it->second.rx_buf.skip(n_copy);
            it->second.rx_done = true;
            F_LOG_T(domain_->ctx->logger, "received ACK: " << (*seq_no & 0x7fff));

            // It's possible that the RX operation completes before the TX operation
            if (it->second.tx_done) {
                auto op = it->second;
                expected_acks_.erase(it);
                op.callback.invoke_and_clear({op.handle(), kStreamOk, op.tx_buf.begin(), op.rx_buf.begin()});
            }
        }

#else
        F_LOG_E(domain_->ctx->logger, "received ack but client support is not compiled in");
#endif

    } else {

#if FIBRE_ENABLE_SERVER
        if (rx_buf.size() < 6) {
            F_LOG_E(domain_->ctx->logger, "packet too short");
            rx_channel_->start_read(rx_buf_, &dummy, MEMBER_CB(this, on_read_finished));
            return;
        }

        // TODO: think about some kind of ordering guarantees
        // currently the seq_no is just used to associate a response with a request

        uint16_t endpoint_id = *read_le<uint16_t>(&rx_buf);
        bool expect_response = endpoint_id & 0x8000;
        endpoint_id &= 0x7fff;

        if (expect_response && tx_handle_) {
            // The operation expects a response but the output channel is still
            // busy. Stop receiving for now. This function will be invoked again
            // once the TX operation is finished.
            rx_end_ = result.end;
            return;
        }

        // Verify packet trailer. The expected trailer value depends on the selected endpoint.
        // For endpoint 0 this is just the protocol version, for all other endpoints it's a
        // CRC over the entire JSON descriptor tree (this may change in future versions).
        uint16_t expected_trailer = endpoint_id ? fibre::json_crc_ : PROTOCOL_VERSION;
        uint16_t actual_trailer = *(rx_buf.end() - 2) | (*(rx_buf.end() - 1) << 8);
        if (expected_trailer != actual_trailer) {
            F_LOG_D(domain_->ctx->logger, "trailer mismatch for endpoint " << endpoint_id << ": expected " << as_hex(expected_trailer) << ", got " << as_hex(actual_trailer));
            rx_channel_->start_read(rx_buf_, &dummy, MEMBER_CB(this, on_read_finished));
            return;
        }
        F_LOG_D(domain_->ctx->logger, "trailer ok for endpoint " << endpoint_id);

        // TODO: if more bytes than the MTU were requested, should we abort or just return as much as possible?

        uint16_t expected_response_length = *read_le<uint16_t>(&rx_buf);

        // Limit response length according to our local TX buffer size
        if (expected_response_length > tx_mtu_ - 2)
            expected_response_length = tx_mtu_ - 2;

        cbufptr_t input_buffer{rx_buf.begin(), rx_buf.end() - 2};
        bufptr_t output_buffer{tx_buf_ + 2, expected_response_length};
        F_LOG_IF_ERR(domain_->ctx->logger,
                     server_.endpoint_handler(domain_, endpoint_id, &input_buffer, &output_buffer),
                     "endpoint handler failed");
        

        // Send response
        if (expect_response) {
            size_t actual_response_length = expected_response_length - output_buffer.size() + 2;
            write_le<uint16_t>(*seq_no | 0x8000, tx_buf_);

            F_LOG_D(domain_->ctx->logger, "send packet: " << as_hex(cbufptr_t{tx_buf_, actual_response_length}));
            tx_channel_->start_write({tx_buf_, actual_response_length}, &tx_handle_, MEMBER_CB(this, on_write_finished));
        }
#else
        F_LOG_E(domain_->ctx->logger, "received request but server support is not compiled in");
#endif
    }

    rx_channel_->start_read(rx_buf_, &dummy, MEMBER_CB(this, on_read_finished));
}

void LegacyProtocolPacketBased::on_rx_closed(StreamStatus status) {
    if (tx_handle_) {
        // TX operation still in progress - cancel TX operation and defer closing
        // the protocol instance until the TX operation has finished.
        rx_status_ = status;
        tx_channel_->cancel_write(tx_handle_);
    } else {
        // No TX operation in progress - close protocol instance immediately.
        on_rx_tx_closed(status);
    }
}

void LegacyProtocolPacketBased::on_rx_tx_closed(StreamStatus status) {
    if (status == kStreamClosed || status == kStreamCancelled) {
        // TODO: handle app-initiated cancellation via cancel_endpoint_operation() (currently unused)
        status = kStreamError;
    }

#if FIBRE_ENABLE_CLIENT
    // Cancel pending endpoint operation
    for (auto& op: pending_operations_) {
        op.callback.invoke_and_clear({op.handle(), status, op.tx_buf.begin(), op.rx_buf.begin()});
    }
    pending_operations_.clear();

    // Cancel all ongoing endpoint operations
    for (auto& item: expected_acks_) {
        if (item.second.callback.has_value()) {
            item.second.callback.invoke_and_clear({item.second.handle(), status, item.second.tx_buf.begin(), item.second.rx_buf.begin()});
        }
    }
    expected_acks_.clear();

    // Report that the root object was lost
    if (client_.root_obj_) {
        auto root_obj = client_.root_obj_;
        client_.root_obj_ = nullptr;
        domain_->on_lost_root_object(reinterpret_cast<Object*>(root_obj.get()));
    }
#endif
    on_stopped_.invoke_and_clear(this, status);
}

void LegacyProtocolPacketBased::start(Callback<void, LegacyProtocolPacketBased*, StreamStatus> on_stopped) {
    on_stopped_ = on_stopped;
    TransferHandle dummy;
    rx_channel_->start_read(rx_buf_, &dummy, MEMBER_CB(this, on_read_finished));

#if FIBRE_ENABLE_CLIENT
    if (on_stopped_.has_value()) {
        client_.start(nullptr, domain_, MEMBER_CB(this, start_call), std::string{intf_name_} + " (legacy protocol)");
    }
#endif
}
