
#include <fibre/config.hpp>

#if FIBRE_ENABLE_SOCKET_CAN_BACKEND

#include "socket_can.hpp"
//#include "../interfaces/canbus.hpp"
#include "can_adapter.hpp"
#include <fibre/domain.hpp>
#include <fibre/fibre.hpp>
#include <fibre/logging.hpp>
#include <fibre/rich_status.hpp>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <linux/if.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <fnmatch.h>
#include <ifaddrs.h>
#include <algorithm>
#include "../print_utils.hpp"

using namespace fibre;

// Resources:
//  https://www.kernel.org/doc/Documentation/networking/can.txt
//  https://www.beyondlogic.org/example-c-socketcan-code/
//
// Creating a local virtual CAN bus:
//   sudo ip link add dev vcan0 type vcan
//   sudo ip link set vcan0 mtu 72
//   sudo ifconfig vcan0 up

static struct canfd_frame convert_message(const can_Message_t& message) {
    struct canfd_frame frame = {
        .can_id = message.id
                | (message.is_extended_id ? CAN_EFF_FLAG : 0)
                | (message.rtr ? CAN_RTR_FLAG : 0),
        .len = message.len,
        .flags = message.bit_rate_switching ? (uint8_t)CANFD_BRS : (uint8_t)0U
    };
    std::copy_n(message.buf, message.len, frame.data);
    return frame;
}

static can_Message_t convert_message(const struct canfd_frame& frame, size_t frame_size) {
    can_Message_t message;
    message.id = frame.can_id & CAN_EFF_MASK,
    message.is_extended_id = (frame.can_id & CAN_EFF_FLAG) != 0,
    message.rtr = (frame.can_id & CAN_RTR_FLAG) != 0,
    message.bit_rate_switching = (frame.flags & CANFD_BRS) != 0,
    message.fd_frame = frame_size == sizeof(struct canfd_frame),
    message.len = frame.len,
    std::copy_n(frame.data, frame.len, message.buf);
    return message;
}


RichStatus SocketCan::init(EventLoop* event_loop, Logger logger, std::string name, Callback<void, SocketCan*> on_error) {
    event_loop_ = event_loop;
    logger_ = logger;
    on_error_ = on_error;

    if ((socket_id_ = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW)) < 0) {
        return F_MAKE_ERR("socket() failed: " << sys_err());
    }

    RichStatus status;

    // Allocate a timer for each TX slot
    for (auto& slot: tx_slots_) {
        slot.timer = nullptr;
    }
    for (auto& slot: tx_slots_) {
        if ((status = event_loop_->open_timer(&slot.timer, MEMBER_CB(&slot, on_timeout))).is_error()) {
            goto fail;
        }
    }
    
    {
        // Switch socket into CAN FD mode (this can happen before it's bound to an interface)
        int enable_canfd = 1;
        if (setsockopt(socket_id_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd))) {
            status = F_MAKE_ERR("setsockopt(CAN_RAW_FD_FRAMES) failed: " << sys_err());
            goto fail;
        }

        // Receive own messages to detect when a transmission succeeded. This is
        // different from loopback mode.
        int recv_own_msgs = 1;
        if (setsockopt(socket_id_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs))) {
            status = F_MAKE_ERR("setsockopt(CAN_RAW_RECV_OWN_MSGS) failed: " << sys_err());
            goto fail;
        }

        // Subscribe to errors. These don't correspond 1:1 to error frames on the
        // bus for instance a bus off condition is delivered as an error message.
        can_err_mask_t err_mask = CAN_ERR_MASK;
        if (setsockopt(socket_id_, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask))) {
            status = F_MAKE_ERR("setsockopt(CAN_RAW_ERR_FILTER) failed: " << sys_err());
            goto fail;
        }

        // Disable all reception (will be enabled by subscribe())
        //if (setsockopt(socket_id_, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0)) {
        //    status = F_MAKE_ERR("setsockopt(CAN_RAW_FILTER) failed: " << sys_err());
        //    goto fail;
        //}

        // Check if the interface supports CANFD
        struct ifreq ifr;
        strcpy(ifr.ifr_name, name.c_str());
        if (ioctl(socket_id_, SIOCGIFMTU, &ifr) < 0) {
            status = F_MAKE_ERR("ioctl() failed: " << sys_err());
            goto fail;
        }

        if (ifr.ifr_mtu != CANFD_MTU) {
            status = F_MAKE_ERR("CAN interface is not CAN FD capable");
            goto fail;
        }

        // Get interface index
        ifr = {};
        strcpy(ifr.ifr_name, name.c_str());
        if (ioctl(socket_id_, SIOCGIFINDEX, &ifr) < 0) {
            status = F_MAKE_ERR("ioctl() failed: " << sys_err());
            goto fail;
        }

        struct sockaddr_can addr;
        memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (bind(socket_id_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            status = F_MAKE_ERR("bind() failed: " << sys_err());
            goto fail;
        }
        
        event_loop_ = event_loop;
        if ((status = event_loop_->register_event(socket_id_, EPOLLIN, MEMBER_CB(this, on_event))).is_error()) {
            goto fail;
        }
    }

    return RichStatus::success();

fail:
    for (auto& slot: tx_slots_) {
        if (slot.timer) {
            F_LOG_IF_ERR(logger_, event_loop_->close_timer(slot.timer), "failed to close timer");
        }
    }
    ::close(socket_id_);
    socket_id_ = 0;
    return status;
}


bool SocketCan::read_sync() {
    struct canfd_frame frame;

    struct iovec vec = {.iov_base = &frame, .iov_len = sizeof(frame)};
    struct msghdr message = {
        .msg_iov = &vec,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0
    };
    ssize_t n_received = recvmsg(socket_id_, &message, 0);

    if (n_received < 0) {
        // If recvfrom returns -1 an errno is set to indicate the error.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false; // no message received
        } else {
            F_LOG_E(logger_, "Socket read failed: " << sys_err());
        }

    } else if (message.msg_flags & MSG_CONFIRM) {
        // TODO: this check doesn't work for arbitrary message sizes
        auto it = std::find_if(tx_slots_.begin(), tx_slots_.end(), [&](TxSlot& slot) {
            return slot.busy && memcmp(&frame, &slot.frame, sizeof(frame)) == 0;
        });

        if (it != tx_slots_.end()) {
            F_LOG_IF_ERR(logger_, it->timer->set(0.0f, TimerMode::kNever), "failed to disable timer");
            on_sent(&*it, true);
        } else {
            F_LOG_W(logger_, "got sent confirmation for unknown message");
        }

    } else if (n_received != sizeof(struct can_frame) && n_received != sizeof(struct canfd_frame)) {
        F_LOG_W(logger_, "invalid message length " << n_received);
        return true;

    } else {
        // Trigger all matching subscriptions
        can_Message_t msg = convert_message(frame, n_received);

        std::vector<on_received_cb_t> triggered;
        for (auto subscription: subscriptions_) {
            if (check_match(subscription->filter, msg)) {
                triggered.push_back(subscription->on_received);
            }
        }
        for (auto s: triggered) {
            s.invoke(msg);
        }
    }

    return true;
}

bool SocketCan::is_valid_baud_rate(uint32_t nominal_baud_rate, uint32_t data_baud_rate) {
    return false; // TODO
}

bool SocketCan::start(uint32_t nominal_baud_rate, uint32_t data_baud_rate, on_event_cb_t rx_event_loop, on_error_cb_t on_error) {
    return false; // TODO
}

bool SocketCan::stop() {
    for (auto& slot: tx_slots_) {
        F_LOG_IF_ERR(logger_, event_loop_->close_timer(slot.timer), "failed to cancel timer");
        if (slot.busy) {
            slot.busy = false;
            slot.on_sent.invoke_and_clear(false);
        }
    }

    return true;
}

bool SocketCan::send_message(uint32_t tx_slot, const can_Message_t& message, on_sent_cb_t on_sent) {
    if (message.len > (message.fd_frame ? CANFD_MAX_DLEN : CAN_MAX_DLEN)) {
        return false;
    }
    if (tx_slot >= tx_slots_.size()) {
        return false;
    }

    // The callback of the old message is overridden
    tx_slots_[tx_slot].on_sent = on_sent;

    if (tx_slots_[tx_slot].busy) {
        can_Message_t msg = message;
        tx_slots_[tx_slot].pending = msg;
    } else {
        tx_slots_[tx_slot].busy = true;
        send_message_now(tx_slot, message);
    }

    return true;
}

bool SocketCan::cancel_message(uint32_t tx_slot) {
    if (F_LOG_IF(logger_, !tx_slots_[tx_slot].busy, "TX slot not active")) {
        return false;
    }

    // We can't really cancel the message that's already dispatched to the
    // network subsystem, so we just disable the on_sent callback so once the
    // message does get sent we don't notify the source anymore.

    tx_slots_[tx_slot].on_sent = {};
    tx_slots_[tx_slot].pending = std::nullopt;
    return true;
}

bool SocketCan::subscribe(uint32_t rx_slot, const MsgIdFilterSpecs& filter, on_received_cb_t on_received, CanSubscription** handle) {
    Subscription* s = new Subscription{ .filter = filter, .on_received = on_received };
    subscriptions_.push_back(s);
    if (handle) {
        *handle = (CanSubscription*)s;
    }

    update_filters();
    return true;
}

bool SocketCan::unsubscribe(CanSubscription* handle) {
    auto it = std::find(subscriptions_.begin(), subscriptions_.end(), (Subscription*)handle);
    if (it == subscriptions_.end()) {
        return false; // subscription not found
    }
    delete *it;
    subscriptions_.erase(it);
    update_filters();
    return true;
}

void SocketCan::send_message_now(uint32_t tx_slot, const can_Message_t& message) {
    TxSlot& slot = tx_slots_[tx_slot]; // deleted in on_timeout()

    // We keep a copy of this frame so once we get a send confirmation we can
    // correlate it with the original send request.
    static_assert(sizeof(slot.frame) == sizeof(struct canfd_frame), "invalid frame size");
    struct canfd_frame& frame = *(struct canfd_frame*)slot.frame;
    frame = convert_message(message);

    slot.parent = this;

    // Start timer to cancel send request after a fixed timeout.
    if (F_LOG_IF_ERR(logger_, slot.timer->set(0.5f, TimerMode::kOnce), "failed to start timer")) {
        return;
    }

    ssize_t msg_len = message.fd_frame ? sizeof(struct canfd_frame) : sizeof(struct can_frame);

    if (write(socket_id_, &frame, msg_len) != msg_len) {
        F_LOG_E(logger_, "write() failed: " << sys_err());
        return; // TODO: handle gracefully
    }

    F_LOG_D(logger_, "sent message");
    return;
}

void SocketCan::on_sent(TxSlot* slot, bool success) {
    if (slot->pending.has_value()) {
        can_Message_t pending = slot->pending.value();
        slot->pending = std::nullopt;
        send_message_now(slot - &tx_slots_[0], pending);
    } else {
        slot->busy = false;
        slot->on_sent.invoke_and_clear(success);
    }
}

void SocketCan::update_filters() {
    struct can_filter filters[subscriptions_.size()];
    memset(filters, 0, sizeof(filters));

    for (size_t i = 0; i < subscriptions_.size(); ++i) {
        if (subscriptions_[i]->filter.id.index() == 0) { // standard ID
            filters[i].can_id = std::get<0>(subscriptions_[i]->filter.id);
            filters[i].can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | subscriptions_[i]->filter.mask;
        } else { // extended ID
            filters[i].can_id = std::get<1>(subscriptions_[i]->filter.id) | CAN_EFF_FLAG;
            filters[i].can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | subscriptions_[i]->filter.mask;
        }
    }

    if (setsockopt(socket_id_, SOL_CAN_RAW, CAN_RAW_FILTER, filters, sizeof(filters))) {
        F_LOG_E(logger_, "could not refresh filters: " << sys_err());
    }
}

void SocketCan::on_event(uint32_t mask) {
    if (mask & EPOLLIN) {
        // Read as many messages as available to increase the change that they
        // are handled before the timeout in case that's already pending.
        while (read_sync());
    }
    if (mask & EPOLLERR) { // This happens when the interface disappears
        event_loop_->deregister_event(socket_id_);
        F_LOG_W(logger_, "interface disappeared");
        on_error_.invoke_and_clear(this); // this will delete the "this" instance
        return;
    }

    if (mask & ~(EPOLLIN | EPOLLERR)) {
        F_LOG_W(logger_, "unexpected event " << mask);
        event_loop_->deregister_event(socket_id_); // deregister to prevent busy spin
    }
}

void SocketCan::on_timeout(TxSlot* slot) {
    // The timeout can trigger simultaneously with the send confirmation but
    // execute earlier (especially after pausing during debugging). In this case
    // we want to check the input buffer before we actually trigger the timeout.


    on_sent(slot, false);
}

RichStatus SocketCanBackend::init(EventLoop* event_loop, Logger logger) {
    event_loop_ = event_loop;
    logger_ = logger;
    return RichStatus::success();
}

RichStatus SocketCanBackend::deinit() {
    return RichStatus::success();
}

void SocketCanBackend::start_channel_discovery(Domain* domain, const char* specs, size_t specs_len, ChannelDiscoveryContext** handle) {
    std::string intf_name;
    if (try_parse_key(specs, specs + specs_len, "if", &intf_name)) {
        domain_ = domain;
        wait_for_intf(intf_name);
    }
}

RichStatus SocketCanBackend::stop_channel_discovery(ChannelDiscoveryContext* handle) {
    RichStatus status;
    RichStatus status2;
    
    if ((status2 = event_loop_->deregister_event(netlink_id_)).is_error() && status.is_success()) {
        status = status2;
    }

    if (::close(netlink_id_) == -1 && status.is_success()) {
        status = F_MAKE_ERR("failed to close socket: " << sys_err{});
    }

    auto intfs = known_interfaces_;
    known_interfaces_.clear();
    for (auto& intf: intfs) {
        SocketCan* socket_can = intf.second.first;
        CanAdapter* can_adapter = intf.second.second;

        can_adapter->stop();
        delete can_adapter;

        socket_can->stop();
        delete socket_can;
    }

    F_LOG_D(domain_->ctx->logger, "stopped SocketCAN backend");

    return status;
}

RichStatus SocketCanBackend::wait_for_intf(std::string intf_name_pattern) {
    intf_name_pattern_ = intf_name_pattern;

    netlink_id_ = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (netlink_id_ < 0) {
        return F_MAKE_ERR("socket() failed: " << sys_err());
    }

    RichStatus status;

    struct sockaddr_nl addr;
    memset((void *)&addr, 0, sizeof(addr));

    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = RTMGRP_LINK; // | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    if (bind(netlink_id_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        status = F_MAKE_ERR("bind() failed: " << sys_err());
        goto fail;
    }

    if ((status = event_loop_->register_event(netlink_id_, EPOLLIN, MEMBER_CB(this, on_event))).is_error()) {
        goto fail;
    }

    {
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1) {
            status = F_MAKE_ERR("bind() failed: " << sys_err());
            goto fail;
        }

        /* Walk through linked list, maintaining head pointer so we
            can free list later */
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            F_LOG_D(logger_, "found interface " << ifa->ifa_name << " (" << (ifa->ifa_flags & IFF_UP ? "up" : "down") << ")");
            F_LOG_D(logger_, "flags: " << as_hex(ifa->ifa_flags));
            if (ifa->ifa_flags & IFF_UP) {
                consider_intf(ifa->ifa_name);
            }
        }

        freeifaddrs(ifaddr);
    }

    return RichStatus::success();

fail:
    ::close(netlink_id_);
    return status;
}

void SocketCanBackend::consider_intf(const char* name) {
    if (known_interfaces_.find(name) != known_interfaces_.end()) {
        // interface already known (it is re-announced e.g. if the link status changes)
        return;
    }

    int match = fnmatch(intf_name_pattern_.c_str(), name, 0);
    if (match == FNM_NOMATCH) {
        F_LOG_D(logger_, "ignoring interface " << name);
        return; // no match - ignore interface
    } else if (match != 0) {
        F_LOG_W(logger_, "fnmatch() failed");
        return;
    }

    // The socket deletes itself when it is closed (TODO: not yet implemented)
    F_LOG_D(logger_, "opening interface " << name);

    auto c = new SocketCan{}; // deleted in on_intf_error() or stop_channel_discovery()
    if (!F_LOG_IF_ERR(logger_, c->init(event_loop_, logger_, name, MEMBER_CB(this, on_intf_error)), "failed to init interface")) {
        auto p = new CanAdapter{event_loop_, domain_, c, name};
        known_interfaces_[name] = {c, p};
        p->start(0, 128);
    } else {
        delete c;
    }
}

void SocketCanBackend::on_intf_error(SocketCan* intf) {
    using T = decltype(*known_interfaces_.begin());
    auto it = std::find_if(known_interfaces_.begin(), known_interfaces_.end(), [&](T& val) {
        return val.second.first == intf;
    });
    if (it == known_interfaces_.end()) {
        F_LOG_E(logger_, "unknown interface failed");
        return;
    }

    F_LOG_D(logger_, it->first << " closed");

    auto val = it->second;
    known_interfaces_.erase(it);

    val.second->stop();
    delete val.second;

    intf->stop();
    delete intf;
}

bool SocketCanBackend::on_netlink_msg() {
    // adapted from https://stackoverflow.com/a/27169191/3621512

    char buf[4096];
    struct iovec iov = { buf, sizeof buf };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1
    };

    int status = recvmsg(netlink_id_, &msg, 0);

    if (status < 0) {
        F_LOG_W(logger_, "netlink event");
        return false;
    } else if (status == 0) {
        return true;
    }

    for (struct nlmsghdr* h = (struct nlmsghdr *)buf; NLMSG_OK(h, (unsigned int)status); h = NLMSG_NEXT(h, status)) {
        switch (h->nlmsg_type) {
            case NLMSG_DONE: {
                return true;
            }

            case NLMSG_ERROR: {
                F_LOG_W(logger_, "netlink reported an error");
                return false; // don't know what to do with this
            }

            case RTM_NEWLINK: {
                struct ifinfomsg* ifi = (struct ifinfomsg *)NLMSG_DATA(h);
                char ifname[1024];
                if (if_indextoname(ifi->ifi_index, ifname) == nullptr) {
                    F_LOG_W(logger_, "error getting interface name");
                }

                F_LOG_D(logger_, "new link: " << ifname << " (" << (ifi->ifi_flags & IFF_UP ? "up" : "down") << ")");
                if (ifi->ifi_flags & IFF_UP) {
                    consider_intf(ifname);
                }

            } break;

            case RTM_DELLINK: {
                F_LOG_D(logger_, "removed link");
            } break;

            default: {
                F_LOG_W(logger_, "unhandled netlink message " << h->nlmsg_type);
                return false;
            }
        }
    }

    return true;
}

void SocketCanBackend::on_event(uint32_t mask) {
    bool success = true;

    if (mask & EPOLLIN) {
        if (!on_netlink_msg()) {
            success = false;
        }
    }

    if (mask & ~(EPOLLIN)) {
        F_LOG_W(logger_, "unexpected event " << mask);
        success = false;
    }

    if (!success) {
        event_loop_->deregister_event(netlink_id_); // deregister to prevent busy spin
    }
}

#endif
