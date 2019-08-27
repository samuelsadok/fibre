#ifndef __FIBRE_CHANNEL_DISCOVERER_HPP
#define __FIBRE_CHANNEL_DISCOVERER_HPP

namespace fibre {

class interface_specs {};

/**
 * @brief Abstract class that provides an interface to find/spawn Fibre channels
 */
class ChannelDiscoverer {
    int init();
    int deinit();
    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    int stop_channel_discovery(void* discovery_ctx);
};

}

#endif // __FIBRE_CHANNEL_DISCOVERER_HPP