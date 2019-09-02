
#include <fibre/usb_discoverer.hpp>
#include <fibre/worker.hpp>

#include <unistd.h>

using namespace fibre;

int usb_start_stop_test() {
    Worker worker;
    if (worker.init() != 0) {
        printf("worker init failed.\n");
        return -1;
    }

    USBHostSideDiscoverer discoverer;
    if (discoverer.init(&worker) != 0) {
        printf("Discoverer init failed.\n");
        return -1;
    }

    void* ctx;
    if (discoverer.start_channel_discovery(nullptr, &ctx) != 0) {
        printf("Discoverer start failed.\n");
        return -1;
    }

    printf("USB is running.\n");

    if (discoverer.stop_channel_discovery(ctx) != 0) {
        printf("Discoverer stop failed.\n");
        return -1;
    }

    if (discoverer.deinit() != 0) {
        printf("Discoverer deinit failed.\n");
        return -1;
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
        return -1;
    }

    return 0;
}

int main(int argc, const char** argv) {
    for (size_t i = 0; i < 10; ++i)
        if (usb_start_stop_test() != 0)
            return -1;

    Worker worker;
    if (worker.init() != 0) {
        printf("worker init failed.\n");
        return -1;
    }

    USBHostSideDiscoverer discoverer;
    if (discoverer.init(&worker) != 0) {
        printf("Discoverer init failed.\n");
        return -1;
    }

    void* ctx;
    if (discoverer.start_channel_discovery(nullptr, &ctx) != 0) {
        printf("Discoverer start failed.\n");
        return -1;
    }

    printf("Waiting for hotplug events. Press [ENTER] to quit.\n");
    getchar(); // TODO: make stdin unbuffered
    printf("exit...\n");

    /*

    // The UDP discoverer, if active, creates a channel that will broadcast on
    // the network.
    // ==> This may not be desired because of privacy. May just want to create
    // input channels that listen for broadcasts.
    fibre::add_discoverer(new fibre::udp_broadcast_discoverer());

    // The USB discoverer, if active, examines each USB device to see if it has
    // a compatible interface. If so, channels for the endpoints are registered.
    fibre::add_discoverer(new fibre::usb_device_discoverer(worker));

    // The Bluetooth LE discoverer scans for BLE services that match the fibre
    // service description. If it finds one, it automatically connects and opens
    // input and output channels.
    fibre::add_discoverer(new fibre::bluetooth_scan_discoverer());

    fibre::add_discoverer(new fibre::cache_discoverer());*/

    if (discoverer.stop_channel_discovery(ctx) != 0) {
        printf("Discoverer stop failed.\n");
    }

    if (discoverer.deinit() != 0) {
        printf("Discoverer deinit failed.\n");
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
    }

    return 0;
}
