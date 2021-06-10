
#include "mock_can.hpp"
#include <fibre/logging.hpp>
#include <algorithm>

using namespace fibre;
using namespace fibre::simulator;

static uint32_t get_arbitration_field(const can_Message_t& msg) {
    uint32_t arbitration_field =
        msg.is_extended_id ? (((msg.id & 0x1FFC0000) | (msg.id & 0x3ffff)) << 3)
                           : (msg.id << 21);
    arbitration_field |= msg.is_extended_id ? 0x200000 : 0;
    arbitration_field |= msg.fd_frame ? 2 : 0;
    arbitration_field |= msg.bit_rate_switching ? 1 : 0;
    return arbitration_field;
}

SimCanInterface* CanMedium::new_intf(Node* node, std::string port_name) {
    Port* port = new Port{node, port_name};
    node->ports[port_name] = port;

    auto bus =
        new CanBus(this, node->name + "." +
                             port_name);  // deleted when removed from busses
    busses_[node->name + "." + port_name] = bus;

    auto intf = new SimCanInterface{};
    intf->bus_ = bus;
    intf->port_ = port;
    bus->members_.push_back(intf);

    return intf;
}

void CanMedium::join(std::vector<std::string> busses, std::string joined_bus) {
    std::vector<CanBus*> old_busses;
    std::vector<CanBus*> transmitters;
    for (auto name : busses) {
        CanBus* b = busses_[name];
        old_busses.push_back(b);
        busses_.erase(name);
        if (b->busy && b->current_transmitter_) {
            transmitters.push_back(b);
        }
    }

    CanBus* new_bus = nullptr;

    if (transmitters.size() == 1) {
        // If exactly one bus has a message in transit at the time of joining,
        // continue transmitting this message.
        new_bus = transmitters[0];
        new_bus->name = joined_bus;
    } else {
        new_bus =
            new CanBus(this, joined_bus);  // deleted when removed from busses
        busses_[joined_bus] = new_bus;
    }

    for (auto b : old_busses) {
        if (b != new_bus) {
            for (auto m : b->members_) {
                new_bus->members_.push_back(m);
                m->bus_ = new_bus;
            }

            if (b->busy) {
                simulator_->cancel(
                    b->current_event_);  // TODO: inform transmitting interface
            }
        }
    }

    for (auto b : old_busses) {
        delete b;
    }
}

void CanMedium::dispatch() {
    dispatch_event_ = nullptr;
    for (auto& b : busses_) {
        if (!b.second->busy) {
            b.second->send_next();
        }
    }
}

void CanMedium::on_tx_pending() {
    if (!dispatch_event_) {
        dispatch_event_ =
            simulator_->send(nullptr, {}, 0.0f, MEMBER_CB(this, dispatch));
    }
}

void CanBus::send_next() {
    // Find the pending message with the lowest arbitration field
    uint32_t winner_id = UINT32_MAX;
    std::vector<SimCanInterface::TxIt> winner_msgs;
    std::vector<SimCanInterface*> winner_intfs;

    for (auto& intf : members_) {
        auto it = intf->get_tx_msg();
        if (it != intf->tx_slots_.end()) {
            uint32_t arbitration_field = get_arbitration_field(it->second.msg);
            if (arbitration_field < winner_id) {
                winner_id = arbitration_field;
                winner_msgs = {it};
                winner_intfs = {intf};
            } else if (arbitration_field == winner_id) {
                winner_msgs.push_back(it);
                winner_intfs.push_back(intf);
            }
        }
    }

    // Schedule message on the simulator
    if (winner_msgs.size() == 1) {
        can_Message_t msg = winner_msgs[0]->second.msg;
        SimCanInterface* tx_intf = winner_intfs[0];

        // TODO: more accurate duration modelling
        // TODO: handle mismatch in interface bit rate
        float duration = (float)(msg.len * 8) / (float)tx_intf->data_baud_rate_;

        current_msg_ = msg;
        current_transmitter_ = tx_intf;
        current_receivers_ = {};
        std::vector<Port*> current_receiver_ports;

        for (auto& rx_intf : members_) {
            if (rx_intf != tx_intf && rx_intf->will_ack(msg)) {
                current_receivers_.push_back(rx_intf);
                current_receiver_ports.push_back(rx_intf->port_);
            }
        }

        tx_intf->on_start_tx(winner_msgs[0]);
        current_event_ =
            medium_->simulator_->send(tx_intf->port_, current_receiver_ports,
                                      duration, MEMBER_CB(this, on_sent));
        busy = true;
        F_LOG_D(logger(), "started transmission of message " << msg.id << " from " << tx_intf->port_);

    } else if (winner_msgs.size() > 1) {
        // TODO: handle collision
        F_LOG_E(logger(), "message collision");
    } else {
        busy = false;
        F_LOG_D(logger(), "no messages pending");
    }
}

void CanBus::on_sent() {
    for (auto& intf : current_receivers_) {
        intf->on_finished_rx(current_msg_);
    }

    if (!current_receivers_.size()) {
        F_LOG_D(logger(), "message was not acknowledged by any receiver");
    }

    current_transmitter_->on_finished_tx(current_receivers_.size());

    current_msg_ = {};
    current_transmitter_ = nullptr;
    current_receivers_ = {};
    current_event_ = nullptr;

    send_next();
}

bool SimCanInterface::is_valid_baud_rate(uint32_t nominal_baud_rate,
                                         uint32_t data_baud_rate) {
    // TODO: implement
    return true;
}
bool SimCanInterface::start(uint32_t nominal_baud_rate, uint32_t data_baud_rate,
                            on_event_cb_t rx_event_loop,
                            on_error_cb_t on_error) {
    // TODO: get rid of RX event loop argument
    nominal_baud_rate_ = nominal_baud_rate;
    data_baud_rate_ = data_baud_rate;
    return true;
}
bool SimCanInterface::stop() {
    // TODO: implement
    return false;
}

bool SimCanInterface::send_message(uint32_t tx_slot,
                                   const can_Message_t& message,
                                   on_sent_cb_t on_sent) {
    tx_slots_[tx_slot] = {message, on_sent};
    bus_->medium_->on_tx_pending();
    return true;
}

bool SimCanInterface::cancel_message(uint32_t tx_slot) {
    auto it = tx_slots_.find(tx_slot);
    if (it == tx_slots_.end()) {
        F_LOG_E(port_->node->logger(), "attempt to cancel inactive TX slot");
        return false;
    }

    tx_slots_.erase(it);
    return true;
}

bool SimCanInterface::subscribe(uint32_t rx_slot,
                                const MsgIdFilterSpecs& filter,
                                on_received_cb_t on_received,
                                CanSubscription** handle) {
    subscriptions_.push_back({filter, on_received});
    return true;
}

bool SimCanInterface::unsubscribe(CanSubscription* handle) {
    // TODO: implement
    return false;
}

SimCanInterface::TxIt SimCanInterface::get_tx_msg() {
    SimCanInterface::TxIt result = tx_slots_.end();
    size_t lowest_id = SIZE_MAX;
    for (auto it = tx_slots_.begin(); it != tx_slots_.end(); ++it) {
        if (get_arbitration_field(it->second.msg) <= lowest_id) {
            lowest_id = get_arbitration_field(it->second.msg);
            result = it;
        }
    }
    return result;
}

void SimCanInterface::on_start_tx(TxIt tx) {
    current_tx_slot_ = tx->first;
}

void SimCanInterface::on_finished_tx(bool ackd) {
    if (ackd) {
        // TODO: this does not mirror actual behavior accurately
        auto tx = tx_slots_[current_tx_slot_];
        tx_slots_.erase(current_tx_slot_);
        tx.on_sent.invoke(true);
    }
}

bool SimCanInterface::will_ack(const can_Message_t& msg) {
    return std::any_of(subscriptions_.begin(), subscriptions_.end(),
                       [&](Rx& sub) { return check_match(sub.filter, msg); });
}

void SimCanInterface::on_finished_rx(const can_Message_t& msg) {
    for (auto& sub : subscriptions_) {
        if (check_match(sub.filter, msg)) {
            sub.on_received.invoke(msg);
        }
    }
}
