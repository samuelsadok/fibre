
#include "legacy_protocol.hpp"
#include "print_utils.hpp"
#include <fibre/config.hpp>
#include <fibre/domain.hpp>
#include <fibre/endpoint_connection.hpp>
#include <fibre/fibre.hpp>
#include <fibre/simple_serdes.hpp>
#include <algorithm>

using namespace fibre;

#if FIBRE_ENABLE_SERVER

struct NewEndpoint0Handler : Socket {
    WriteResult write(WriteArgs args) final;
    WriteArgs on_write_done(WriteResult result) final;

    Socket* socket_;

    uint8_t buf[4];
    size_t buf_pos = 0;

    Chunk response_[1];
    CBufIt response_pos_;

    static const uint8_t version_id_buf[4];

private:
    // void write_handler(WriteArgs args);
    // write_handler();
};

const uint8_t NewEndpoint0Handler::version_id_buf[4] = {
    (uint8_t)((json_version_id_ >> 0) & 0xff),
    (uint8_t)((json_version_id_ >> 8) & 0xff),
    (uint8_t)((json_version_id_ >> 16) & 0xff),
    (uint8_t)((json_version_id_ >> 24) & 0xff),
};

void write_handler(WriteArgs args) {}

WriteResult NewEndpoint0Handler::write(WriteArgs args) {
    while (args.buf.n_chunks()) {
        if (args.buf.front().is_buf() && args.buf.front().layer() == 0) {
            Chunk chunk = args.buf.front();
            size_t n_copy = std::min(sizeof(buf), chunk.buf().size());
            std::copy_n(chunk.buf().begin(), n_copy, buf + buf_pos);
            buf_pos += n_copy;

        } else {
            // ignore unrecognized chunks
        }

        args.buf = args.buf.skip_chunks(1);
    }

    if (args.status == kFibreClosed) {
        uint32_t offset = 0;
        if (buf_pos == sizeof(buf)) {
            read_le<uint32_t>(&offset, buf);
        }

        if (offset == 0xffffffff) {
            // If the offset is special value 0xFFFFFFFF, send back
            // the JSON version ID instead
            response_[0] = Chunk(0, version_id_buf);
        } else {
            offset = std::min((size_t)offset, embedded_json_length);
            response_[0] = Chunk(0, {embedded_json + offset,
                                     embedded_json + embedded_json_length});
        }

        response_pos_ = BufChain{response_}.begin();

        WriteResult result;
        do {
            result =
                socket_->write({{response_pos_, response_ + 1}, kFibreClosed});
            if (result.is_busy()) {
                break;
            }
            response_pos_ = result.end;
        } while (BufChain{response_pos_, response_ + 1}.n_chunks() ||
                 result.status != kFibreClosed);
    }

    return {args.status, args.buf.begin()};
}

WriteArgs NewEndpoint0Handler::on_write_done(WriteResult result) {
    response_pos_ = result.end;
    return {{response_pos_, response_ + 1}, kFibreClosed};
}

Cont EndpointServerConnection::rx_logic(WriteArgs args) {
    if (!rx_active) {
        for (;;) {
            if (!args.buf.n_chunks()) {
                return Cont1{args.status, args.buf.begin()};
            }

            Chunk chunk = args.buf.front();
            args.buf = args.buf.skip_chunks(1);

            if (chunk.is_buf() && chunk.layer() == 0) {
                size_t n_copy =
                    std::min(sizeof(buf), chunk.buf().size() - buf_offset);
                std::copy_n(chunk.buf().begin(), n_copy, buf + buf_offset);
                buf_offset += n_copy;

                if (buf_offset == sizeof(buf) && n_copy) {
                    cbufptr_t rx_buf = {buf, buf_offset};

                    uint16_t endpoint_id = *read_le<uint16_t>(&rx_buf);
                    endpoint_id &= 0x7fff;

                    // Verify packet trailer. The expected trailer value depends
                    // on the selected endpoint. For endpoint 0 this is just the
                    // protocol version, for all other endpoints it's a CRC over
                    // the entire JSON descriptor tree (this may change in
                    // future versions).
                    uint16_t expected_trailer =
                        endpoint_id ? fibre::json_crc_ : PROTOCOL_VERSION;
                    uint16_t actual_trailer = *read_le<uint16_t>(&rx_buf);
                    if (expected_trailer != actual_trailer) {
                        F_LOG_D(domain_->ctx->logger,
                                "trailer mismatch for endpoint "
                                    << endpoint_id << ": expected "
                                    << as_hex(expected_trailer) << ", got "
                                    << as_hex(actual_trailer));
                        call0.socket_ = nullptr;
                        call0.parent_ = this;
                        call0.footer_pos = BufChain{boundary}.begin();
                        buf_offset = 0;
                        rx_active = true;
                        // In case of an error we still handle the incoming
                        // operation like a normal endpoint operation except
                        // that we discard the data. This ensures that we send
                        // back a frame boundary so the client stays in sync.
                    } else {
                        F_LOG_D(domain_->ctx->logger,
                                "trailer ok for endpoint " << endpoint_id);

                        start_endpoint_operation(endpoint_id & 0x3fff,
                                                 !!(endpoint_id & 0x4000));
                    }
                    break;
                }
            } else if (chunk.is_frame_boundary() && chunk.layer() == 0) {
                buf_offset = 0;
                F_LOG_E(domain_->ctx->logger,
                        "endpoint operation terminated without executing - "
                        "might confuse client");
                // TODO: close connection
            }
        }
    }

    if (!args.buf.n_chunks()) {
        return Cont1{args.status, args.buf.begin()};
    }

    pending = args;
    auto it = args.buf.find_layer0_bound();

    return Cont0{args.buf.until(it.chunk).elevate(-1),
                 it == args.buf.end() ? kFibreOk : kFibreClosed};
}

Cont EndpointServerConnection::rx_logic(WriteResult result) {
    Call* call = &call0;
    pending.buf = pending.buf.from(result.end);
    if (pending.buf.n_chunks() && pending.buf.front().is_frame_boundary() &&
        pending.buf.front().layer() == 0 && result.status == kFibreClosed) {
        pending.buf = pending.buf.skip_chunks(1);  // skip frame boundary
        rx_active = false;
        return Cont0{pending.buf, result.status};
    }
    return rx_logic(pending);
}

WriteResult EndpointServerConnection::on_rx(WriteArgs args) {
    Cont cont = rx_logic(args);

    for (;;) {
        if (cont.index() == 1) {
            return std::get<1>(cont);
        }

        Call* call = &call0;

        WriteResult result;
        if (rx_active && call->socket_) {
            result = call->socket_->write(std::get<0>(cont));
        } else {
            result = {std::get<0>(cont).status, std::get<0>(cont).buf.c_end()};
        }
        if (result.is_busy()) {
            return WriteResult::busy();
        }

        cont = rx_logic(result);
    }
}

Cont EndpointServerConnection::tx_logic(WriteArgs args) {
    Call* call = &call0;

    if (args.buf.n_chunks()) {
        call->pending = args;
        return Cont0{args.buf.elevate(1), kFibreOk};
    } else if (args.status != kFibreOk &&
               call->footer_pos.chunk != boundary + 1) {
        call->pending = args;
        return Cont0{{call->footer_pos, boundary + 1}, kFibreOk};
    } else {
        call->pending = {};
        return Cont1{args.status, args.buf.begin()};
    }
}

Cont EndpointServerConnection::tx_logic(WriteResult result) {
    Call* call = &call0;

    if (call->pending.buf.n_chunks()) {
        call->pending.buf = call->pending.buf.from(result.end);
    } else {
        call->footer_pos = result.end;
        if (call->footer_pos.chunk == boundary + 1) {
            // tx_queue_.erase(tx_queue_.begin());
            return Cont1{call->pending.status, call->pending.buf.begin()};
        }
    }

    return tx_logic(call->pending);
}

WriteResult EndpointServerConnection::Call::write(WriteArgs args) {
    Cont cont = parent_->tx_logic(args);

    for (;;) {
        if (cont.index() == 1) {
            return std::get<1>(cont);
        }

        F_LOG_D(parent_->domain_->ctx->logger,
                "sending to client: " << std::get<0>(cont).buf);

        WriteResult result = parent_->tx(std::get<0>(cont));
        if (result.is_busy()) {
            return WriteResult::busy();
        }

        cont = parent_->tx_logic(result);
    }
}

// Analogous to EndpointClientConnection::Call::on_write_done
WriteArgs EndpointServerConnection::on_tx_done(WriteResult result) {
    Call* call = &call0;
    Cont cont = tx_logic(result);

    for (;;) {
        if (cont.index() == 0) {
            F_LOG_D(domain_->ctx->logger,
                    "sending to client: " << std::get<0>(cont).buf);
            return std::get<0>(cont);
        }

        WriteArgs args = call->socket_->on_write_done(std::get<1>(cont));
        if (args.is_busy()) {
            return WriteArgs::busy();
        }

        if (call0.footer_pos.chunk == boundary + 1) {
            return WriteArgs::busy();
        }

        call = &call0;
        cont = tx_logic(args);
    }
}

WriteArgs EndpointServerConnection::Call::on_write_done(WriteResult result) {
    F_LOG_E(parent_->domain_->ctx->logger, "not implemented");
    return {{}, kFibreClosed};  // TODO
}

void EndpointServerConnection::start_endpoint_operation(uint16_t endpoint_id,
                                                        bool exchange) {
    if (endpoint_id == 0) {
        // TODO: no heap allocation
        auto asd = new NewEndpoint0Handler{};
        asd->socket_ = &call0;
        call0.socket_ = asd;
        call0.parent_ = this;
        call0.footer_pos = BufChain{boundary}.begin();
        buf_offset = 0;
        rx_active = true;

    } else if (endpoint_id >= n_endpoints) {
        F_LOG_E(domain_->ctx->logger, "unknown endpoint");
        return;

    } else {
        ServerFunctionId function_id;
        ServerObjectId object_id;

        if (endpoint_table[endpoint_id].type ==
            EndpointType::kFunctionTrigger) {
            auto trigger = endpoint_table[endpoint_id].function_trigger;
            function_id = trigger.function_id;
            object_id = trigger.object_id;

        } else if (endpoint_table[endpoint_id].type ==
                   EndpointType::kRoProperty) {
            auto ro_property = endpoint_table[endpoint_id].ro_property;
            function_id = ro_property.read_function_id;
            object_id = ro_property.object_id;

        } else if (endpoint_table[endpoint_id].type ==
                   EndpointType::kRwProperty) {
            auto rw_property = endpoint_table[endpoint_id].rw_property;
            function_id = exchange ? rw_property.exchange_function_id
                                   : rw_property.read_function_id;
            object_id = rw_property.object_id;
        } else {
            F_LOG_E(domain_->ctx->logger, "unknown endpoint type");
            return;
        }

        const Function* func = domain_->get_server_function(function_id);
        auto call = func->start_call(domain_, call_frame, &call0);
        call0.socket_ = call;
        call0.parent_ = this;
        call0.footer_pos = BufChain{boundary}.begin();
        buf_offset = 0;
        rx_active = true;

        ServerObjectId id = object_id;
        Chunk chunks[2] = {
            Chunk(0, {reinterpret_cast<uint8_t*>(&id), sizeof(id)}),
            Chunk::frame_boundary(0)};
        call->write({chunks, kFibreOk});
        // TODO: handle the case where the call does not
        // immediately consume the object id

        // TODO: handle property endpoints
    }
}

#endif

#if FIBRE_ENABLE_CLIENT

Socket* EndpointClientConnection::start_call(
    uint16_t ep_num, uint16_t json_crc, std::vector<uint16_t> in_arg_ep_nums,
    std::vector<uint16_t> out_arg_ep_nums, Socket* caller) {
    Call* call = new Call{};  // freed when removed from rx_queue_
    write_le<uint16_t>(ep_num | 0x8000, call->header);
    write_le<uint16_t>(json_crc, call->header + 2);
    call->parent_ = this;
    call->chunks_[0] = Chunk(0, call->header);
    call->caller_ = caller;
    call->header_pos = BufChain{call->chunks_}.begin();
    call->footer_pos = BufChain{boundary}.begin();
    tx_queue_.push_back(call);
    rx_queue_.push_back(call);

    return call;
}

Cont EndpointClientConnection::tx_logic(WriteArgs args) {
    Call* call = tx_queue_.front();

    if (call->header_pos.chunk != call->chunks_ + 1) {
        call->pending = args;
        return Cont0{{call->header_pos, call->chunks_ + 1}, kFibreOk};
    } else if (args.buf.n_chunks()) {
        call->pending = args;
        return Cont0{args.buf.elevate(1), kFibreOk};
    } else if (args.status != kFibreOk &&
               call->footer_pos.chunk != boundary + 1) {
        call->pending = args;
        return Cont0{{call->footer_pos, boundary + 1}, kFibreOk};
    } else {
        call->pending = {};
        return Cont1{args.status, args.buf.begin()};
    }
}

Cont EndpointClientConnection::tx_logic(WriteResult result) {
    Call* call = tx_queue_.front();

    if (call->header_pos.chunk != call->chunks_ + 1) {
        call->header_pos = result.end;
    } else if (call->pending.buf.n_chunks()) {
        call->pending.buf = call->pending.buf.from(result.end);
    } else {
        call->footer_pos = result.end;
        if (call->footer_pos.chunk == boundary + 1) {
            tx_queue_.erase(tx_queue_.begin());
            return Cont1{call->pending.status, call->pending.buf.begin()};
        }
    }

    return tx_logic(call->pending);
}

void EndpointClientConnection::tx_loop() {
    WriteResult result = {kFibreOk, tx_queue_.front()->header_pos};
    for (;;) {
        WriteArgs args = on_tx_done(result);
        if (args.is_busy()) {
            break;
        }
        result = tx(args);
        if (result.is_busy()) {
            break;
        }
    }
}

void EndpointClientConnection::rx_loop(Cont cont) {
    WriteArgs args =
        cont.index() == 0 ? std::get<0>(cont) : rx_done(std::get<1>(cont));

    for (;;) {
        if (args.is_busy()) {
            break;
        }
        WriteResult result = on_rx(args);
        if (result.is_busy()) {
            break;
        }
        args = rx_done(result);
    }
}

WriteResult EndpointClientConnection::Call::write(WriteArgs args) {
    bool is_first =
        parent_->tx_queue_.size() && parent_->tx_queue_.front() == this;
    if (!is_first) {
        // Call is awaiting TX operation
        pending = args;
        return WriteResult::busy();
    }

    Cont cont = parent_->tx_logic(args);

    for (;;) {
        if (cont.index() == 1) {
            // If this call terminated, unblock the waiting calls
            if (parent_->tx_queue_.size() &&
                parent_->tx_queue_.front() != this) {
                parent_->tx_loop();
            }
            return std::get<1>(cont);
        }

        F_LOG_T(parent_->domain_->ctx->logger,
                "sending to server: " << std::get<0>(cont).buf);

        WriteResult result = parent_->tx(std::get<0>(cont));
        if (result.is_busy()) {
            return WriteResult::busy();
        }

        cont = parent_->tx_logic(result);
    }
}

// Analogous to EndpointServerConnection::Call::on_tx_done
WriteArgs EndpointClientConnection::on_tx_done(WriteResult result) {
    Call* call = tx_queue_.front();
    Cont cont = tx_logic(result);

    for (;;) {
        if (cont.index() == 0) {
            F_LOG_T(domain_->ctx->logger,
                    "sending to server: " << std::get<0>(cont).buf);
            return std::get<0>(cont);
        }

        WriteArgs args = call->caller_->on_write_done(std::get<1>(cont));
        if (args.is_busy()) {
            return WriteArgs::busy();
        }

        if (!tx_queue_.size()) {
            return WriteArgs::busy();
        }
        call = tx_queue_.front();

        cont = tx_logic(args);
    }
}

Cont EndpointClientConnection::rx_logic(WriteArgs args) {
    // If the call was already closed by the callee, skip everything until
    // a layer 0 boundary is found
    if (call_closed_) {
        auto it = args.buf.find_layer0_bound();
        args.buf = args.buf.from(it);
        if (it != args.buf.end()) {
            call_closed_ = false;
            args.buf = args.buf.skip_chunks(1);
        }
    }

    pending = args;
    if (pending.buf.n_chunks()) {
        auto it = args.buf.find_layer0_bound();
        return Cont0{args.buf.until(it.chunk).elevate(-1),
                     it == args.buf.end() ? kFibreOk : kFibreClosed};
    } else {
        return Cont1{kFibreOk, pending.buf.begin()};
    }
}

Cont EndpointClientConnection::rx_logic(WriteResult result) {
    pending.buf = pending.buf.from(result.end);
    if (result.status != kFibreOk) {
        delete rx_queue_.front();
        rx_queue_.erase(rx_queue_.begin());

        if (pending.buf.n_chunks() && pending.buf.front().is_frame_boundary() &&
            pending.buf.front().layer() == 0) {
            pending.buf = pending.buf.skip_chunks(1);
        } else {
            call_closed_ = true;
        }
    }

    return rx_logic(pending);
}

WriteResult EndpointClientConnection::on_rx(WriteArgs args) {
    Cont cont = rx_logic(args);

    for (;;) {
        if (cont.index() == 1) {
            return std::get<1>(cont);
        }

        if (!rx_queue_.size()) {
            return WriteResult::busy();
        }
        Call* call = rx_queue_.front();

        F_LOG_T(domain_->ctx->logger,
                "sending to app: " << std::get<0>(cont).buf << ", "
                                   << std::get<0>(cont).status);

        WriteResult result = call->caller_->write(std::get<0>(cont));
        if (result.is_busy()) {
            return WriteResult::busy();
        }

        cont = rx_logic(result);
    }
}

WriteArgs EndpointClientConnection::Call::on_write_done(WriteResult result) {
    EndpointClientConnection* parent = parent_;
    Cont cont =
        parent->rx_logic(result);  // Note: "this" may be deleted within here

    // If this call terminated, unblock the waiting calls
    if (!parent->rx_queue_.size() || parent->rx_queue_.front() != this) {
        parent->rx_loop(cont);
        return {{}, result.status};
    }

    for (;;) {
        if (cont.index() == 0) {
            F_LOG_T(parent->domain_->ctx->logger,
                    "sending to app: " << std::get<0>(cont).buf << ", "
                                       << std::get<0>(cont).status);
            return std::get<0>(cont);
        }

        WriteArgs args = parent->rx_done(std::get<1>(cont));
        if (args.is_busy()) {
            return WriteArgs::busy();
        }

        cont = parent->rx_logic(args);
    }
}

#endif
