#ifndef __FIBRE_USB_DISCOVERER_HPP
#define __FIBRE_USB_DISCOVERER_HPP

#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/channel_discoverer.hpp>

#include <libusb.h>
#include <libudev.h>

namespace fibre {

class USBHostSideDiscoverer : ChannelDiscoverer {
public:
    int init(Worker* worker);
    int deinit();
    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    int stop_channel_discovery(void* discovery_ctx);

private:
    void udev_handler(uint32_t events);
    void usb_handler(uint32_t events);

    void pollfd_added_handler(int fd, short events);
    void pollfd_removed_handler(int fd);

    int hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev,
                     libusb_hotplug_event event);

    int start_udev_monitor();
    int stop_udev_monitor();
    bool is_udev_monitor_started() { return udev_mon; }

    int start_libusb_monitor();
    int stop_libusb_monitor();

    struct udev* udev_ctx = nullptr; // udev context (only used on Linux)
    libusb_context *libusb_ctx = nullptr; // libusb session
    struct udev_monitor* udev_mon = nullptr;
    libusb_hotplug_callback_handle hotplug_callback_handle;
    int n_discovery_requests = 0;
    Worker* worker_ = nullptr;
    Timer timer_;

    // TODO make templated callback type so we can just write Timer::callback_t<USBHostSideDiscoverer>
    Closure<USBHostSideDiscoverer, std::tuple<USBHostSideDiscoverer*>, std::tuple<uint32_t>, void> udev_handler_obj{&USBHostSideDiscoverer::udev_handler, this};
    Closure<USBHostSideDiscoverer, std::tuple<USBHostSideDiscoverer*>, std::tuple<uint32_t>, void> usb_handler_obj{&USBHostSideDiscoverer::usb_handler, this};
};


}

#endif // __FIBRE_USB_DISCOVERER_HPP