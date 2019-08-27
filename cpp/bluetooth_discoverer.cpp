
#include <fibre/bluetooth_discoverer.hpp>
#include "dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"

using namespace fibre;


int BluetoothCentralSideDiscoverer::init(Worker* worker, DBusConnectionWrapper* dbus) {
    worker_ = worker;
    dbus_ = dbus;
    return 0;
}

int BluetoothCentralSideDiscoverer::deinit() {
    worker_ = nullptr;
    dbus_ = nullptr;
    return 0;
}

using fancy_type = std::unordered_map<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>;
fibre::Callback<fancy_type> callback = {
    [](void*, fancy_type objects) {
        printf("got %zu objects\n", objects.size());
        for (auto& it : objects) {
            std::cout << "key: " << it.first << ", value: " << it.second << std::endl;
        }
    }, nullptr
};

int BluetoothCentralSideDiscoverer::start_ble_adapter_monitor() {
    org_freedesktop_DBus_ObjectManager bluez_root_obj(dbus_, "org.bluez", "/");
    //DBusObject bluez(&dbus_connection, "org.bluez", "/org/bluez");

    bluez_root_obj.GetManagedObjects_async(&callback);

    return 0;
}

int BluetoothCentralSideDiscoverer::stop_ble_adapter_monitor() {
    return 0;
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
            printf("Failed to start USB device discovery\n");
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
            printf("Stop USB device discovery\n");
            result = -1;
        }
    }
    n_discovery_requests--;
    return result;
}
