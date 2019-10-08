#ifndef __FIBRE_CHANNEL_DISCOVERER_HPP
#define __FIBRE_CHANNEL_DISCOVERER_HPP

namespace fibre {

class interface_specs {};

/**
 * @brief Abstract class that provides an interface to find/spawn Fibre channels
 */
class ChannelDiscoverer {
public:
    //int init();
    //int deinit();
    //int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    //int stop_channel_discovery(void* discovery_ctx);

private:
    virtual int raise_effort_to_1() { return 0; }
    virtual int raise_effort_to_2() { return 0; }
    virtual int drop_effort_from_2() { return 0; }
    virtual int drop_effort_from_1() { return 0; }

    virtual int raise_effort_to(size_t effort) {
        if (effort == 1)
            return raise_effort_to_1();
        else if (effort == 2)
            return raise_effort_to_2();
        else
            return 0;
    }

    virtual int drop_effort_from(size_t effort) {
        if (effort == 2)
            return drop_effort_from_2();
        else if (effort == 2)
            return drop_effort_from_1();
        else
            return 0;
    }
};

}

#endif // __FIBRE_CHANNEL_DISCOVERER_HPP