
// Peripheral:
//  1. Instantiate org.bluez.GattService1 (contains the local characteristics)
//  2. Register service with org.bluez.GattManager1
//  3. Instantiate org.bluez.LEAdvertisement1
//  4. Register ad with org.bluez.LEAdvertisingManager1
//
// Central:
//  1. Instantiate org.bluez.GattProfile1 (contains a auto-connect UUID list)
//  2. Register profile with org.bluez.GattManager1

#include <fibre/bluetooth_discoverer.hpp>
#include "dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "dbus_interfaces/org.bluez.LEAdvertisingManager1.hpp"
#include "dbus_interfaces/org.bluez.GattManager1.hpp"

using namespace fibre;

DEFINE_LOG_TOPIC(BLUETOOTH);
USE_LOG_TOPIC(BLUETOOTH);

int BluetoothCentralSideDiscoverer::init(Worker* worker, DBusConnectionWrapper* dbus) {
    worker_ = worker;
    dbus_ = dbus;
    bluez_root_obj_.~DBusRemoteObject();
    new (&bluez_root_obj_) decltype(bluez_root_obj_){{dbus_, "org.bluez", "/"}};
    return 0;
}

int BluetoothCentralSideDiscoverer::deinit() {
    worker_ = nullptr;
    dbus_ = nullptr;
    return 0;
}

void BluetoothCentralSideDiscoverer::handle_adapter_found(adapter_t* adapter) {
    FIBRE_LOG(D) << "found BLE adapter " << adapter->base_;
}

void BluetoothCentralSideDiscoverer::handle_adapter_lost(adapter_t* adapter) {
    FIBRE_LOG(D) << "lost BLE adapter " << adapter->base_;
}

int BluetoothCentralSideDiscoverer::start_ble_adapter_monitor() {
    if (!dbus_) {
        FIBRE_LOG(E) << "discoverer object not initialized";
        return -1;
    }
    return dbus_discoverer_.start(&bluez_root_obj_, &handle_adapter_found_obj_, &handle_adapter_lost_obj_);
}

int BluetoothCentralSideDiscoverer::stop_ble_adapter_monitor() {
    return dbus_discoverer_.stop();
}

/**
 * @brief Starts creating channels that might help in finding the requested kind of objects.
 * 
 * If the function succeeds, an opaque context pointer is returned which must be
 * passed to stop_channel_discovery() to terminate this particular request.
 */
int BluetoothCentralSideDiscoverer::start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx) {
    // if there are already discovery requests in place, there's nothing to do
    if (!n_discovery_requests) {
        if (start_ble_adapter_monitor() != 0) {
            FIBRE_LOG(E) << "Failed to start bluetooth device discovery";
            return -1;
        }
    }
    n_discovery_requests++;
    return 0;
}

/**
 * @brief Stops an object discovery process that was started with start_channel_discovery()
 */
int BluetoothCentralSideDiscoverer::stop_channel_discovery(void* discovery_ctx) {
    int result = 0;
    if (n_discovery_requests == 1) {
        if (stop_ble_adapter_monitor() != 0) {
            FIBRE_LOG(E) << "Failed to stop bluetooth device discovery";
            result = -1;
        }
    }
    n_discovery_requests--;
    return result;
}
