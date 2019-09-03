
#include <fibre/bluetooth_discoverer.hpp>
#include "dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"

using namespace fibre;


int BluetoothCentralSideDiscoverer::init(Worker* worker, DBusConnectionWrapper* dbus) {
    worker_ = worker;
    dbus_ = dbus;
    bluez_root_obj.~org_freedesktop_DBus_ObjectManager();
    new (&bluez_root_obj) org_freedesktop_DBus_ObjectManager{dbus_, "org.bluez", "/"};
    return 0;
}

int BluetoothCentralSideDiscoverer::deinit() {
    worker_ = nullptr;
    dbus_ = nullptr;
    return 0;
}

using interface_map = std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>;

static fibre::Callback<DBusObject, interface_map> handle_interfaces_added = {
    [](void*, DBusObject object, interface_map interfaces) {
        std::cout << "DBus Object " << object << " obtained the interfaces " << interfaces;
    }, nullptr
};

static fibre::Callback<DBusObject, std::vector<std::string>> handle_interfaces_removed = {
    [](void*, DBusObject object, std::vector<std::string> interfaces) {
        std::cout << "DBus Object " << object << " lost the interfaces " << interfaces;
    }, nullptr
};

static fibre::Callback<std::unordered_map<DBusObject, interface_map>> handle_initial_search_completed = {
    [](void*, std::unordered_map<DBusObject, interface_map> objects) {
        printf("got %zu objects\n", objects.size());
        for (auto& it : objects) {
            // TODO: make nicer interface to invoke callback
            handle_interfaces_added.callback(nullptr, it.first, it.second);
        }
    }, nullptr
};

int BluetoothCentralSideDiscoverer::start_ble_adapter_monitor() {
    if (!bluez_root_obj.conn_) {
        std::cerr << "discoverer object not initialized\n";
        return -1;
    }
    //DBusObject bluez(&dbus_connection, "org.bluez", "/org/bluez");

    bluez_root_obj.InterfacesAdded += &handle_interfaces_added;
    bluez_root_obj.InterfacesRemoved += &handle_interfaces_removed;
    //bluez_root_obj.GetManagedObjects_async(&handle_initial_search_completed);
    // TODO: error handling

    return 0;
}

int BluetoothCentralSideDiscoverer::stop_ble_adapter_monitor() {
    bluez_root_obj.InterfacesAdded += &handle_interfaces_added;
    bluez_root_obj.InterfacesRemoved += &handle_interfaces_removed;
    // TODO: cancel call to GetManagedObjects_async
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
