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

    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx) {
        if (raise_effort_to(1)) {
            return -1;
        }
        if (raise_effort_to(2)) {
            return -1;
        }
        return 0;
    }
    int stop_channel_discovery(void* discovery_ctx) {
        int result = 0;
        if (drop_effort_from(2)) {
            result = -1;
        }
        if (drop_effort_from(1)) {
            result = -1;
        }
        return result;
    }

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
        else if (effort == 1)
            return drop_effort_from_1();
        else
            return 0;
    }
};

}

#endif // __FIBRE_CHANNEL_DISCOVERER_HPP