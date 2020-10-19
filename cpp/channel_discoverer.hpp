#ifndef __FIBRE_CHANNEL_DISCOVERER
#define __FIBRE_CHANNEL_DISCOVERER

#include "async_stream.hpp"
#include <fibre/libfibre.h>

namespace fibre {

struct ChannelDiscoveryResult {
    FibreStatus status;
    AsyncStreamSource* rx_channel;
    AsyncStreamSink* tx_channel;
};

}

#endif // __FIBRE_CHANNEL_DISCOVERER