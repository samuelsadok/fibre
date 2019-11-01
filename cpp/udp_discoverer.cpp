
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fibre/udp_discoverer.hpp>
#include <fibre/logging.hpp>
#include <fibre/calls.hpp>
#include <fibre/dispatcher.hpp>

const std::tuple<std::string, int> kMulticastAddrTx = {"::FFFF:239.83.132.50", 39245};
const std::tuple<std::string, int> kMulticastAddrRx = {"::FFFF:239.83.132.50", 39245};
//const std::tuple<std::string, int> kMulticastAddr = {"239.83.132.50", 39245};

//const std::tuple<std::string, int> kMulticastAddrTx = {"10.5.83.134", 39245};
//const std::tuple<std::string, int> kMulticastAddrRx = {"::", 39245};

//const std::tuple<std::string, int> kMulticastAddr = {"ff12::1234", 39245};
//const std::tuple<std::string, int> kMulticastAddr = {"::", 39245};

DEFINE_LOG_TOPIC(UDP_DISCOVERER);
USE_LOG_TOPIC(UDP_DISCOVERER);

namespace fibre {

int UDPDiscoverer::init(PosixSocketWorker* worker) {
    if (worker_) {
        FIBRE_LOG(E) << "already initialized";
        return -1;
    }
    worker_ = worker;
    return 0;
}

int UDPDiscoverer::deinit() {
    if (!worker_) {
        FIBRE_LOG(E) << "not initialized";
        return -1;
    }
    worker_ = nullptr;
    return 0;
}

int UDPDiscoverer::raise_effort_to_1() {
    FIBRE_LOG(D) << "init UDP receiver";
    if (rx_channel_.open(kMulticastAddrRx)) {
        FIBRE_LOG(E) << "failed to init UDP receiver";
        return -1;
    }
    if (rx_channel_.subscribe(worker_, &rx_handler_obj)) {
        FIBRE_LOG(E) << "failed to init UDP receiver";
        goto fail;
    }
    return 0;

fail:
    rx_channel_.close();
    return -1;
}

int UDPDiscoverer::raise_effort_to_2() {
    FIBRE_LOG(D) << "init UDP sender";
    if (tx_channel_.open(kMulticastAddrTx)) {
        return -1;
    }

    // Make shared pointer without delete capability
    std::shared_ptr<MultiFragmentEncoder> tx_channel_ptr(&tx_channel_encoder, [](MultiFragmentEncoder* ptr){});

    main_dispatcher.add_tx_channel(tx_channel_ptr);

    // TODO: should we receive on the same socket? If we use the same address
    // this should already be covered by the effort level 1.
    return 0;
}

int UDPDiscoverer::drop_effort_from_2() {
    FIBRE_LOG(D) << "close UDP sender";
    return tx_channel_.close();
}

int UDPDiscoverer::drop_effort_from_1() {
    FIBRE_LOG(D) << "close UDP receiver";
    int result = 0;
    if (rx_channel_.unsubscribe()) {
        result = -1;
    }
    if (rx_channel_.close()) {
        result = -1;
    }
    return result;
}


void UDPDiscoverer::rx_handler(StreamSource::status_t status, cbufptr_t bufptr) {
    FIBRE_LOG(E) << "UDP received something!";
    MemoryStreamSource src{bufptr.ptr, bufptr.length};

    // TODO: remove dynamic allocation
    auto tx_channel = new PosixUdpTxChannel();
    
    if (tx_channel->open(rx_channel_)) {
        goto fail;
    }

    // TODO: this pattern of creating a shared_ptr also exists when allocating an
    // allocating outgoing call struct. Should maybe be replaced with a handle
    // table or so.
    {
        std::shared_ptr<StreamSink> tx_channel_ptr(tx_channel, [](StreamSink* ptr){
            FIBRE_LOG(D) << "closing temp UDP TX channel";
            auto tx_channel = dynamic_cast<PosixUdpTxChannel*>(ptr);
            if (tx_channel->close()) {
                FIBRE_LOG(E) << "failed to close temp UDP TX channel";
            }
            delete tx_channel;
        });

        Context ctx;
        ctx.add_tx_channel(tx_channel_ptr); // TODO: create new TX channel instead of adding this one.
        CRCMultiFragmentDecoder::decode_fragments(bufptr, &ctx);
    }

    return;

fail:
    delete tx_channel;
}

}
