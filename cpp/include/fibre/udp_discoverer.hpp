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
    void rx_handler(StreamSource::status_t status, cbufptr_t bufptr);

    PosixSocketWorker* worker_ = nullptr;

    PosixUdpRxChannel rx_channel_{}; // active at effort level 1
    PosixUdpTxChannel tx_channel_{}; // active at effort level 2

    CRCMultiFragmentEncoder tx_channel_encoder{{&tx_channel_, [](StreamSink*){}}, nullptr};

    member_closure_t<decltype(&UDPDiscoverer::rx_handler)> rx_handler_obj{&UDPDiscoverer::rx_handler, this};
};

}

#endif // __FIBRE_UDP_TRANSPORT_HPP