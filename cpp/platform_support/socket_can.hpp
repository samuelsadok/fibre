#ifndef __FIBRE_SOCKET_CAN_HPP
#define __FIBRE_SOCKET_CAN_HPP

#include <fibre/config.hpp>

#if FIBRE_ENABLE_SOCKET_CAN_BACKEND

#include <fibre/channel_discoverer.hpp>
#include <fibre/event_loop.hpp>
#include <fibre/logging.hpp>
#include <fibre/backport/optional.hpp>
#include "../interfaces/canbus.hpp"
#include <string>
#include <vector>

namespace fibre {

struct CanAdapter;

class SocketCan final : public CanInterface {
public:
    RichStatus init(EventLoop* event_loop, Logger logger, std::string intf_name, Callback<void, SocketCan*> on_error);

    bool is_valid_baud_rate(uint32_t nominal_baud_rate, uint32_t data_baud_rate) final;
    bool start(uint32_t nominal_baud_rate, uint32_t data_baud_rate, on_event_cb_t rx_event_loop, on_error_cb_t on_error) final;
    bool stop() final;
    bool send_message(uint32_t tx_slot, const can_Message_t& message, on_sent_cb_t on_sent) final;
    bool cancel_message(uint32_t tx_slot) final;
    bool subscribe(uint32_t rx_slot, const MsgIdFilterSpecs& filter, on_received_cb_t on_received, CanSubscription** handle) final;
    bool unsubscribe(CanSubscription* handle) final;

private:
    struct Subscription {
        MsgIdFilterSpecs filter;
        on_received_cb_t on_received;
    };

    struct TxSlot {
        bool busy = false;
        uint8_t frame[72];
        Timer* timer;
        SocketCan* parent;
        on_sent_cb_t on_sent;
        std::optional<can_Message_t> pending;

        void on_timeout() { parent->on_timeout(this); }
    };

    void send_message_now(uint32_t tx_slot, const can_Message_t& message);
    void on_sent(TxSlot* slot, bool success);
    void update_filters();
    bool read_sync();
    void on_event(uint32_t mask);
    void on_timeout(TxSlot* tx_slot);

    EventLoop* event_loop_ = nullptr;
    Logger logger_ = Logger::none();
    int socket_id_ = -1;
    Callback<void, SocketCan*> on_error_;
    std::vector<Subscription*> subscriptions_;

    // The number of TX slots is chosen somewhat arbitrarily. We want to have
    // enough slots to keep the FIFOs from running dry (e.g. on the path down to
    // a USB-CAN dongle) but not too many to keep the buffers from throwing an
    // overflow error.
    std::array<TxSlot, 128> tx_slots_;
};

class SocketCanBackend : public Backend {
public:
    RichStatus init(EventLoop* event_loop, Logger logger) final;
    RichStatus deinit() final;

    void start_channel_discovery(Domain* domain, const char* specs, size_t specs_len, ChannelDiscoveryContext** handle) final;
    RichStatus stop_channel_discovery(ChannelDiscoveryContext* handle) final;
    
private:
    RichStatus wait_for_intf(std::string intf_name_pattern);
    void consider_intf(const char* name);
    void on_intf_error(SocketCan* intf);
    bool on_netlink_msg();
    void on_event(uint32_t mask);

    EventLoop* event_loop_ = nullptr;
    Logger logger_ = Logger::none();
    Domain* domain_;
    std::string intf_name_pattern_;
    int netlink_id_ = -1;
    std::unordered_map<std::string, std::pair<SocketCan*, CanAdapter*>> known_interfaces_;
};

}

#endif

#endif // __FIBRE_SOCKET_CAN_HPP
