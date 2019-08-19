
#include <fibre/dbus.hpp>
#include <dbus/dbus.h>
#include <sys/epoll.h>
#include <fibre/worker.hpp>
#include <fibre/timer.hpp>

#include <unistd.h>

using namespace fibre;

int main(int argc, const char** argv) {
    Worker worker;
    if (worker.init() != 0) {
        printf("worker init failed.\n");
        return -1;
    }

    DBusConnectionWrapper dbus_connection;
    if (dbus_connection.init(&worker) != 0) {
        printf("DBus init failed.\n");
        return -1;
    }


    printf("dispatch message...\n");
    DBusObject bluez_root_obj(&dbus_connection, "org.bluez", "/");
    DBusObject bluez(&dbus_connection, "org.bluez", "/org/bluez");

    Callback<const char*> callback = {
        [](void*, const char* xml) { printf("XML: %s", xml); }, nullptr
    };
    get_managed_objects_async(&bluez_root_obj, &callback);

    printf("waiting for a bit...\n");
    //usleep(3000000);
    getchar(); // TODO: make stdin unbuffered
    printf("done...\n");




    if (dbus_connection.deinit() != 0) {
        printf("Discoverer deinit failed.\n");
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
    }

    return 0;
}
