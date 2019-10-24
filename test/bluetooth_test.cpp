
// note: on my machine the bluetooth card is on usb1/1-8, it can thus be removed
// and added back via
//
// echo "1-8" | sudo tee /sys/bus/usb/drivers/usb/unbind
// echo "1-8" | sudo tee /sys/bus/usb/drivers/usb/bind

#include <fibre/platform_support/linux_worker.hpp>
#include <fibre/platform_support/dbus.hpp>

#include <fibre/bluetooth_discoverer.hpp>

using namespace fibre;

int main(int argc, const char** argv) {
    LinuxWorker worker;
    if (worker.init() != 0) {
        printf("worker init failed.\n");
        return -1;
    }

    DBusConnectionWrapper dbus_connection;
    if (dbus_connection.init(&worker, true) != 0) {
        printf("DBus init failed.\n");
        return -1;
    }


    /*printf("dispatch message...\n");
    org_freedesktop_DBus_Introspectable bluez_root_obj(&dbus_connection, "org.bluez", "/");
    //DBusObject bluez(&dbus_connection, "org.bluez", "/org/bluez");

    Callback<const char*> callback = {
        [](void*, const char* xml) { printf("XML: %s", xml); }, nullptr
    };
    bluez_root_obj.Introspect_async(&callback);
    bluez_root_obj.Introspect_async(&callback);
    bluez_root_obj.Introspect_async(&callback);*/

    BluetoothCentralSideDiscoverer bluetooth_discoverer;
    if (bluetooth_discoverer.init(&worker, &dbus_connection) != 0) {
        printf("Discoverer init failed\n");
        return -1;
    }

    void* ctx;
    if (bluetooth_discoverer.start_channel_discovery(nullptr, &ctx) != 0) {
        printf("Discoverer start failed\n");
        return -1;
    }

    printf("waiting for a bit...\n");
    //usleep(3000000);
    getchar(); // TODO: make stdin unbuffered
    printf("done...\n");




    if (bluetooth_discoverer.deinit() != 0) {
        printf("Discoverer deinit failed.\n");
    }

    if (dbus_connection.deinit() != 0) {
        printf("Connection deinit failed.\n");
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
    }

    return 0;
}
