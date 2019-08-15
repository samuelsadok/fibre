#ifndef __FIBRE_USB_DISCOVERER_HPP
#define __FIBRE_USB_DISCOVERER_HPP

#include <fibre/worker.hpp>

#include <libusb.h>
#include <libudev.h>

namespace fibre {


class interface_specs {};

/**
 * @brief Abstract class that provides an interface to find/spawn Fibre channels
 */
class ChannelDiscoverer {
    int init();
    int deinit();
    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    int stop_channel_discovery(void* discovery_ctx);
};

class USBHostSideDiscoverer : ChannelDiscoverer {
public:
    int init(Worker* worker);
    int deinit();
    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    int stop_channel_discovery(void* discovery_ctx);

private:
    void udev_handler();
    void timer_handler();
    void usb_handler();

    void pollfd_added_handler(int fd, short events);
    void pollfd_removed_handler(int fd);

    int hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev,
                     libusb_hotplug_event event);

    int start_udev_monitor();
    int stop_udev_monitor();
    bool is_udev_monitor_started() { return udev_mon; }

    int start_timer();
    int stop_timer();
    bool is_timer_started() { return tim_fd >= 0; }

    int start_libusb_monitor();
    int stop_libusb_monitor();

    struct udev* udev_ctx = nullptr; // udev context (only used on Linux)
    libusb_context *libusb_ctx = nullptr; // libusb session
    struct udev_monitor* udev_mon = nullptr;
    libusb_hotplug_callback_handle hotplug_callback_handle;
    int n_discovery_requests = 0;
    int tim_fd = -1;
    Worker* worker_ = nullptr;

    Worker::callback_t udev_handler_obj = {
        .callback = [](void* ctx){ ((USBHostSideDiscoverer*)ctx)->udev_handler(); },
        .ctx = this
    };

    Worker::callback_t timer_handler_obj = {
        .callback = [](void* ctx){ ((USBHostSideDiscoverer*)ctx)->timer_handler(); },
        .ctx = this
    };

    Worker::callback_t usb_handler_obj = {
        .callback = [](void* ctx){ ((USBHostSideDiscoverer*)ctx)->usb_handler(); },
        .ctx = this
    };
};


}

#endif // __FIBRE_USB_DISCOVERER_HPP