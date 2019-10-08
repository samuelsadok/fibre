#ifndef __FIBRE_UDP_TRANSPORT_HPP
#define __FIBRE_UDP_TRANSPORT_HPP

#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/channel_discoverer.hpp>

#include <libusb.h>
#include <libudev.h>

namespace fibre {

class UDPTXChannel : public TXChannel {
public:
    int init(int socket_fd, struct sockaddr_in6 remote_addr);
    int deinit();
    size_t get_mtu();
    StreamSink::status_t tx(const uint8_t* buffer, size_t length, size_t* processed_bytes) override;

private:
    int socket_fd_;
    struct sockaddr_in6 remote_addr_;
};

class UDPRXChannel {
public:
    int init(Worker* worker_, int socket_fd);
    int deinit();

private:
    void rx_handler();

    Worker* worker_ = nullptr;
    int socket_fd_ = -1;
    InputChannelDecoder input_channel_;
};

class UDPDiscoverer : public ChannelDiscoverer {
public:
    int init();
    int deinit();

private:
    int raise_effort_to_1() override;
    int raise_effort_to_2() override;
    int drop_effort_from_2() override;
    int drop_effort_from_1() override;

private:
    Worker* worker_ = nullptr;
    int socket1_fd_ = -1;
    int socket2_fd_ = -1;

    UDPRXChannel rx_channel_{}; // active at effort level 1
    UDPTXChannel tx_channel_{}; // active at effort level 2
};

}

#endif // __FIBRE_UDP_TRANSPORT_HPP