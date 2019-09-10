
#include <fibre/bluetooth_discoverer.hpp>
#include "dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"

using namespace fibre;

DEFINE_LOG_TOPIC(BLUETOOTH);
USE_LOG_TOPIC(BLUETOOTH);

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

static auto handle_interfaces_added = make_lambda_closure(
    [](DBusObject object, interface_map interfaces) {
            FIBRE_LOG(D) << "DBus Object " << object << " obtained the interfaces " << interfaces;
    }
);

static auto handle_interfaces_removed = make_lambda_closure(
    [](DBusObject object, std::vector<std::string> interfaces) {
        FIBRE_LOG(D) << "DBus Object " << object << " lost the interfaces " << interfaces;
    }
);

static auto handle_initial_search_completed = make_lambda_closure(
    [](std::unordered_map<DBusObject, interface_map> objects) {
        FIBRE_LOG(D) << "found " << objects.size() << " objects";
        for (auto& it : objects) {
            // TODO: make nicer interface to invoke callback
            handle_interfaces_added(it.first, it.second);
        }
    }
);

int BluetoothCentralSideDiscoverer::start_ble_adapter_monitor() {
    if (!bluez_root_obj.conn_) {
        FIBRE_LOG(E) << "discoverer object not initialized";
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
    bluez_root_obj.InterfacesAdded -= &handle_interfaces_added;
    bluez_root_obj.InterfacesRemoved -= &handle_interfaces_removed;
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
