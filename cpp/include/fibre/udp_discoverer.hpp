#ifndef __FIBRE_UDP_TRANSPORT_HPP
#define __FIBRE_UDP_TRANSPORT_HPP

#include <fibre/platform_support/posix_udp.hpp>
#include <fibre/channel_discoverer.hpp>
#include <fibre/context.hpp>
#include <fibre/stream.hpp>
#include <fibre/calls.hpp>

#include <netinet/in.h>

namespace fibre {

class UDPDiscoverer : public ChannelDiscoverer {
public:
    int init(PosixSocketWorker* worker);
    int deinit();

private:
    int raise_effort_to_1() override;
    int raise_effort_to_2() override;
    int drop_effort_from_2() override;
    int drop_effort_from_1() override;

private:
    // TODO: UDPDiscoverer could directly implement a forwarding StreamSink which forwards data to the inner layers
    StreamStatus rx_handler(cbufptr_t bufptr);

    StreamStatus get_buffer_handler(bufptr_t* bufptr);
    StreamStatus commit_handler(size_t length);
    void completed_handler(StreamStatus status);
    uint8_t rx_buffer_[65536]; // TODO: remove

    PosixSocketWorker* worker_ = nullptr;

    PosixUdpRxChannel rx_channel_{}; // active at effort level 1
    PosixUdpTxChannel tx_channel_{}; // active at effort level 2

    CRCMultiFragmentEncoder tx_channel_encoder{{&tx_channel_, [](StreamSink*){}}, nullptr};

    member_closure_t<decltype(&UDPDiscoverer::get_buffer_handler)> get_buffer_handler_obj{&UDPDiscoverer::get_buffer_handler, this};
    member_closure_t<decltype(&UDPDiscoverer::commit_handler)> commit_handler_obj{&UDPDiscoverer::commit_handler, this};
    member_closure_t<decltype(&UDPDiscoverer::completed_handler)> completed_handler_obj{&UDPDiscoverer::completed_handler, this};
};

}

#endif // __FIBRE_UDP_TRANSPORT_HPP