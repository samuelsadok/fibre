#ifndef __FIBRE_SIM_MOCK_CAN_HPP
#define __FIBRE_SIM_MOCK_CAN_HPP

#include "simulator.hpp"
#include <fibre/../../interfaces/canbus.hpp>
#include <fibre/async_stream.hpp>
#include <memory>

namespace fibre {
namespace simulator {

struct CanBus;
struct CanMedium;

class SimCanInterface : public CanInterface {
public:
    bool is_valid_baud_rate(uint32_t nominal_baud_rate,
                            uint32_t data_baud_rate) final;
    bool start(uint32_t nominal_baud_rate, uint32_t data_baud_rate,
               on_event_cb_t rx_event_loop, on_error_cb_t on_error) final;
    bool stop() final;
    bool send_message(uint32_t tx_slot, const can_Message_t& message,
                      on_sent_cb_t on_sent) final;
    bool cancel_message(uint32_t tx_slot) final;
    bool subscribe(uint32_t rx_slot, const MsgIdFilterSpecs& filter,
                   on_received_cb_t on_received,
                   CanSubscription** handle) final;
    bool unsubscribe(CanSubscription* handle) final;

    // private:
    struct TxSlot {
        can_Message_t msg;
        on_sent_cb_t on_sent;
    };

    using TxIt = std::unordered_map<uint32_t, TxSlot>::iterator;

    TxIt get_tx_msg();
    void on_start_tx(TxIt tx);
    void on_finished_tx(bool ackd);
    bool will_ack(const can_Message_t& msg);
    void on_finished_rx(const can_Message_t& msg);

    CanBus* bus_;
    simulator::Port* port_;

    uint32_t current_tx_slot_;
    std::unordered_map<uint32_t, TxSlot> tx_slots_;

    struct Rx {
        MsgIdFilterSpecs filter;
        on_received_cb_t on_received;
    };
    std::vector<Rx> subscriptions_;

    /*
    Callback<void, ReadResult> rx_completer_;
    Callback<void, WriteResult0> tx_completer_;
    cbufptr_t tx_buf_;

    can_Message_t rx_msg_;
    bufptr_t rx_buf_;*/

    uint32_t nominal_baud_rate_ = 1000000;
    uint32_t data_baud_rate_ = 1000000;
};

struct CanMedium {
    CanMedium(Simulator* simulator) : simulator_{simulator} {}

    SimCanInterface* new_intf(Node* node, std::string port_name);
    void join(std::vector<std::string> busses, std::string joined_bus);
    // void split(std::string bus, std::string part1, std::string part2); //
    // TODO

    void dispatch();
    void on_tx_pending();

    std::unordered_map<std::string, CanBus*> busses_;
    Simulator* simulator_;
    Simulator::Event* dispatch_event_ = nullptr;
};

struct CanBus : Node {
    CanBus(CanMedium* medium, std::string name)
        : Node{medium->simulator_, name, {}}, medium_{medium} {}

    void on_tx_pending();
    void on_sent();
    void send_next();

    CanMedium* medium_;
    std::vector<SimCanInterface*> members_;
    bool busy = false;

    Simulator::Event* current_event_;
    can_Message_t current_msg_;
    SimCanInterface* current_transmitter_;
    std::vector<SimCanInterface*> current_receivers_;
};

}  // namespace simulator
}  // namespace fibre

#endif  // __FIBRE_SIM_MOCK_CAN_HPP
