
#include <fibre/config.hpp>

#if FIBRE_ENABLE_CAN_ADAPTER

#include "can_adapter.hpp"
#include <fibre/domain.hpp>
#include <fibre/fibre.hpp>
#include <fibre/logging.hpp>
#include <fibre/low_level_protocol.hpp>
#include <fibre/timer.hpp>
#include <algorithm>

using namespace fibre;

void CanAdapter::start(int tx_slots_begin, int tx_slots_end) {
    tx_slots_begin_ = tx_slots_begin;
    tx_slots_end_ = tx_slots_end;

    rng.seed(domain_->node_id[0], domain_->node_id[1], domain_->node_id[2],
             domain_->node_id[3]);

    timer_provider_->open_timer(&timer_, MEMBER_CB(this, on_timer));

    send_acquisition_message_0();

    // Accept all Fibre messages from all nodes to all nodes. We do this so that
    // when another node sends a message to a dead node, that message doesn't
    // clog up the bus when it's being auto-resent.
    // Priorities can't handle this because during normal operation we want
    // heartbeat messages to have low priority.
    MsgIdFilterSpecs filter = {
        .id = (uint32_t)0x1e000000UL,
        .mask = 0x1f000000,
    };
    intf_->subscribe(0, filter, MEMBER_CB(this, on_can_msg),
                     &heartbeat_subscription);
}

void CanAdapter::stop() {
    intf_->unsubscribe(heartbeat_subscription);

    if (sending_heartbeat_) {
        sending_heartbeat_ = false;
        intf_->cancel_message(tx_slots_begin_);
    }
    if (active_mailbox_) {
        active_mailbox_ = nullptr;
        intf_->cancel_message(tx_slots_begin_ + 1);
    }

    for (auto& node : routes_) {
        domain_->on_lost_node(node.second, this);
    }

    timer_provider_->close_timer(timer_);

    F_LOG_D(domain_->ctx->logger, "stopped CAN adapter");
}

can_Message_t CanAdapter::get_heartbeat_message(bool dominant) {
    can_Message_t msg;
    msg.id = 0x1eaaaa00UL | (dominant ? 0 : 0x100) | node_id_;
    msg.is_extended_id = true;
    msg.rtr = false;
    msg.bit_rate_switching = false;
    msg.fd_frame = true;
    msg.len = 16;
    std::copy_n(domain_->node_id.begin(), 16, msg.buf);
    return msg;
}

void CanAdapter::send_acquisition_message_0() {
    F_LOG_T(domain_->ctx->logger, "send_acquisition_message_0");
    // Select random CAN Node ID (this is different from the Fibre Node ID)
    state_ = kJoining0;
    node_id_ = rng.next();
    sending_heartbeat_ = true;
    timer_->set(0.0f, TimerMode::kNever);
    intf_->send_message(tx_slots_begin_, get_heartbeat_message(false),
                        MEMBER_CB(this, on_acquisition_msg_sent_0));
}

void CanAdapter::on_acquisition_msg_sent_0(bool success) {
    F_LOG_T(domain_->ctx->logger, "on_acquisition_msg_sent_0");
    sending_heartbeat_ = false;
    if (success) {
        timer_->set(0.1f, TimerMode::kOnce);
    } else {
        send_acquisition_message_0();
    }
}

void CanAdapter::send_acquisition_message_1() {
    F_LOG_T(domain_->ctx->logger, "send_acquisition_message_1");
    sending_heartbeat_ = true;
    intf_->send_message(tx_slots_begin_, get_heartbeat_message(false),
                        MEMBER_CB(this, on_acquisition_msg_sent_1));
}

void CanAdapter::on_acquisition_msg_sent_1(bool success) {
    F_LOG_T(domain_->ctx->logger, "on_acquisition_msg_sent_1");
    sending_heartbeat_ = false;
    if (success) {
        // We're done allocating a CAN ID. However we're not allowed to send
        // data yet because the other nodes on the bus don't know what Fibre
        // NodeID CAN ID belongs to. For this we need to send a heartbeat first.

        // TODO: only send heartbeat if in discoverable mode
        state_ = kJoining1;
        timer_->set(0.1f, TimerMode::kPeriodic);
        send_heartbeat();
    } else {
        send_acquisition_message_0();
    }
}

void CanAdapter::send_heartbeat() {
    F_LOG_D(domain_->ctx->logger, "send_heartbeat");
    sending_heartbeat_ = true;
    intf_->send_message(tx_slots_begin_, get_heartbeat_message(true),
                        MEMBER_CB(this, on_heartbeat_sent));
}

void CanAdapter::on_timer() {
    if (state_ == kJoining0) {
        send_acquisition_message_1();
    } else {
        send_heartbeat();
    }
}

void CanAdapter::on_heartbeat_sent(bool success) {
    sending_heartbeat_ = false;
    if (success) {
        F_LOG_D(domain_->ctx->logger, "sent heartbeat");

        if (state_ != kOperational) {
            // We're done allocating a CAN ID! We can now start sending under
            // this ID.
            F_LOG_D(domain_->ctx->logger,
                    "now operational with node ID " << (int)node_id_);

            state_ = kOperational;
            for (auto& m : mailboxes) {
                if (!send_now(&m)) {
                    // TODO: propagate to message source
                    F_LOG_E(domain_->ctx->logger, "send error");
                    mailboxes.free(&m);
                }
            }
        }

    } else {
        // It's possible that the message collided with another heartbeat
        // message so to be safe we back off and select a new ID.
        send_acquisition_message_0();
    }
}

void CallContext::reset_at(Domain* domain, uint8_t layer) {
    if (layer <= 1) {
        routing_info_offset = 0;
    }
    if (layer <= 0) {
        domain->close_call(handler);
        handler = nullptr;
    }
}

void CanAdapter::on_can_msg(const can_Message_t& msg) {
    // TODO: this discards messages if they come in fast. Need to fetch messages
    // from CAN bus on demand or buffer them in this class.

    if ((msg.id & 0x1fffff00) == 0x1eaaab00UL) {
        // node ID acquisition message - ignore (TODO)
        F_LOG_D(domain_->ctx->logger, "received node acquisition message");
    } else if ((msg.id & 0x1fffff00) == 0x1eaaaa00UL) {
        uint8_t can_id = msg.id & 0xff;
        F_LOG_D(domain_->ctx->logger,
                "received heartbeat from node ID " << (int)can_id);

        if (msg.len >= 16) {
            NodeId fibre_id;
            std::copy_n(msg.buf, 16, fibre_id.begin());

            auto it = routes_.find(can_id);
            if (it == routes_.end() || it->second->id != fibre_id) {
                // the CAN ID is not known or not associated with the Fibre ID
                // specified in the message

                if (it != routes_.end()) {
                    // the CAN ID was reassigned to a new Fibre node
                    // TODO: inform fibre about lost node
                    routes_.erase(it);
                }

                auto ptr = routes_.alloc(can_id, nullptr);
                if (ptr) {
                    F_LOG_D(domain_->ctx->logger, "this node is new");
                    domain_->on_found_node(fibre_id, this, intf_name_, ptr);
                    if (!*ptr) {
                        routes_.erase(routes_.find(can_id));
                    }
                } else {
                    F_LOG_W(domain_->ctx->logger, "too many CAN nodes");
                }
            }  // else the fibre node is already known - ignore
        } else {
            F_LOG_W(domain_->ctx->logger,
                    "invalid heartbeat length: " << msg.len);
        }
    } else if (state_ == kOperational &&
               ((msg.id & 0x1f00ff00) == (0x1e000000 | (uint32_t)(node_id_ << 8)))) {
        uint8_t can_id = msg.id & 0xff;
        uint8_t slot_id = (msg.id >> 16) & 0xff;

        F_LOG_D(domain_->ctx->logger, "got message from " << (int)can_id);

        auto route_it = routes_.find(can_id);
        if (route_it == routes_.end()) {
            F_LOG_W(domain_->ctx->logger, "data from unknown CAN node");
            return;
        }

        Node* node = route_it->second;

        CallContext* ctx = rx_slots.get({slot_id, can_id, {}});
        if (!ctx) {
            // this slot is unknown - alloc new slot
            ctx = rx_slots.alloc({slot_id, can_id, {}});
            if (!ctx) {
                F_LOG_W(domain_->ctx->logger, "too many input streams on CAN");
                return;
            }
        }

        BufChainStorage<10> buf_chain;

        Chunk chunks[10];
        BufChainBuilder builder{chunks};
        write_iterator write_it{builder};

        uint8_t reset_layer = 0;
        if (!LowLevelProtocol::unpack(ctx->state, {msg.buf, msg.len},
                                      &reset_layer, write_it)) {
            F_LOG_E(domain_->ctx->logger, "failed to unpack message");
            return;
        }

        BufChain chain = builder;

        if (reset_layer != 0xff) {
            ctx->reset_at(domain_, reset_layer);
        }

        while (chain.n_chunks()) {
            Chunk chunk = chain.front();

            if (chunk.layer() <= 1 && chunk.is_frame_boundary()) {
                ctx->reset_at(domain_, chunk.layer());
                chain = chain.skip_chunks(1);
            } else if (chunk.layer() == 0) {
                // ignore data on layer0
                chain = chain.skip_chunks(1);
            } else if (chunk.layer() == 1) {
                // data on layer 1
                size_t n_copy =
                    std::min(chunk.buf().size(), sizeof(ctx->routing_info) -
                                                     ctx->routing_info_offset);
                std::copy_n(chunk.buf().begin(), n_copy,
                            ctx->routing_info + ctx->routing_info_offset);
                ctx->routing_info_offset += n_copy;

                if (ctx->routing_info_offset >= 1) {
                    if (ctx->routing_info[0] == 0x00 ||
                        ctx->routing_info[0] ==
                            0x01) {  // call ID for local call stream
                        if (ctx->routing_info_offset >= 17) {
                            std::array<uint8_t, 16> call_id;
                            std::copy_n(ctx->routing_info + 1, 16,
                                        call_id.begin());
                            domain_->open_call(
                                call_id, ctx->routing_info[0], this, node,
                                &ctx->handler);  // TODO: log error
                        }
                    }
                }
                chain = chain.skip_chunks(1);

            } else {
                // Handle data addressed to top level protocol
                auto payload_end = chain.find_chunk_on_layer(1);

                if (ctx->handler) {
                    ctx->handler->process_sync(
                        chain.until(payload_end.chunk).elevate(-2));
                } else {
                    // discard data because we don't know what handler to
                    // send it to
                    // TODO: log
                }

                chain = chain.from(payload_end);
            }
        }
    } else {
        F_LOG_W(domain_->ctx->logger,
                "ignoring message not for me: " << msg.id);
    }
}

void CanAdapter::on_can_msg_sent(bool success) {
    if (!active_mailbox_) {
        F_LOG_W(domain_->ctx->logger, "unexpected callback");
        return;
    }

    Mailbox m = *active_mailbox_;
    mailboxes.free(active_mailbox_);
    active_mailbox_ = nullptr;

    if (!success) {
        F_LOG_W(domain_->ctx->logger, "failed to send message");
        return;
    }

    // F_LOG_W(domain_->ctx->logger, "sent message from pipe " << m.task.pipe);
    multiplexer_.on_sent(m.task.pipe, m.end);
}

bool CanAdapter::open_output_slot(uintptr_t* p_slot_id, Node* dest) {
    std::bitset<kMaxOutputSlotsPerDest> slots_in_use;

    for (auto& active_slot : tx_slots) {
        if (active_slot.dest == dest) {
            slots_in_use[active_slot.slot_id] = true;
        }
    }

    uint8_t output_slot_id = find_first(slots_in_use.flip());
    if (output_slot_id >= kMaxOutputSlotsPerDest) {
        return false;  // cannot allocate more output slots for this destination
    }

    TxContext* slot = tx_slots.alloc();  // freed in close_output_slot()
    if (!slot) {
        return false;  // out of memory
    }

    slot->dest = dest;
    slot->slot_id = output_slot_id;

    if (p_slot_id) {
        *p_slot_id = reinterpret_cast<uintptr_t>(slot);
    }

    return true;
}

bool CanAdapter::close_output_slot(uintptr_t slot_id) {
    TxContext* slot = reinterpret_cast<TxContext*>(slot_id);
    tx_slots.free(slot);
    return true;
}

bool CanAdapter::start_write(TxTaskChain tasks) {
    // TODO:
    // specifiy a buffer depth: Some CAN interfaces may benefit from
    // enqueing more than one message at once (e.g. USB CAN dongles).

    Mailbox* m = mailboxes.alloc();
    if (!m) {
        return false;  // busy
    }

    // TODO: check if idx0 is valid
    m->task = tasks[0];

    if (state_ != kOperational) {
        return true;  // will be started when operational_ becomes true
    } else {
        return send_now(m);
    }
}

bool CanAdapter::send_now(Mailbox* m) {
    TxContext* tx_slot = reinterpret_cast<TxContext*>(m->task.slot_id);

    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [&](std::pair<uint8_t, Node*>& item) {
                               return item.second == tx_slot->dest;
                           });
    if (it == routes_.end()) {
        F_LOG_W(domain_->ctx->logger, "no route to host");
        return false;  // no route to host
    }

    uint32_t tx_mailbox =
        tx_slots_begin_ +
        1;  // TODO: select between multiple and consider FIFO-type outputs
    uint32_t rx_slot = 0;  // TODO: load from context

    can_Message_t msg;
    msg.id = ((uint32_t)it->first << 8) | node_id_ | ((uint32_t)rx_slot << 16) |
             0x1e000000UL;
    msg.is_extended_id = true;
    msg.rtr = false;
    msg.bit_rate_switching = true;
    msg.fd_frame = true;

    bufptr_t packet{msg.buf};
    m->end = LowLevelProtocol::pack(tx_slot->state, m->task.chain(), &packet);

    if (packet.begin() == msg.buf) {
        F_LOG_E(domain_->ctx->logger, "failed to pack message");
        return false;
    }

    active_mailbox_ = m;
    msg.len = packet.begin() - msg.buf;
    // F_LOG_W(domain_->ctx->logger, "send message (len " << (int)msg.len << ")
    // from pipe " << m->task.pipe);
    intf_->send_message(tx_mailbox, msg, MEMBER_CB(this, on_can_msg_sent));
    return true;
}

void CanAdapter::cancel_write() {}

#endif
