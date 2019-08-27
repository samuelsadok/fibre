#ifndef __FIBRE_BLUETOOTH_DISCOVERER_HPP
#define __FIBRE_BLUETOOTH_DISCOVERER_HPP

#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/dbus.hpp>
#include <fibre/channel_discoverer.hpp>

#include <libusb.h>
#include <libudev.h>

namespace fibre {

class BluetoothCentralSideDiscoverer : ChannelDiscoverer {
public:
    int init(Worker* worker, DBusConnectionWrapper* dbus);
    int deinit();
    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    int stop_channel_discovery(void* discovery_ctx);

private:
    int start_ble_adapter_monitor();
    int stop_ble_adapter_monitor();

    Worker* worker_ = nullptr;
    DBusConnectionWrapper* dbus_ = nullptr;
    int n_discovery_requests = 0;
};


}

#endif // __FIBRE_BLUETOOTH_DISCOVERER_HPP