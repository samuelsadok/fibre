
#include <fibre/usb_discoverer.hpp>

#include <iostream>
#include <unistd.h>
#include <sys/epoll.h>

using namespace fibre;

/**
 * @brief Initializes the discoverer.
 * 
 * Asynchronous tasks will be executed on the provided worker.
 * 
 * @param worker: The worker that is used to execute background tasks. The
 *        pointer must be non-null and initialized when this function is called.
 *        It must remain initialized until deinit() of this discoverer was called.
 */
int USBHostSideDiscoverer::init(Worker* worker) {
    if (!worker)
        return -1;
    worker_ = worker;
    
    const struct libusb_pollfd** pollfds;

    udev_ctx = udev_new();
    if (!udev_ctx) {
        fprintf(stderr, "udev_new() failed\n");
        goto fail0;
    }

    if (libusb_init(&libusb_ctx) != LIBUSB_SUCCESS) {
        fprintf(stderr, "libusb_init() failed\n");
        goto fail1;
    }

    // Check if libusb needs special time-based polling on this platform
    if (libusb_pollfds_handle_timeouts(libusb_ctx) == 0) {
        fprintf(stderr, "libusb needs time-based polling on this platform, which is not yet implemented\n");
        goto fail2;
    }

    // libusb maintains a (dynamic) list of file descriptors that need to be
    // monitord (via select/poll/epoll) so that I/O events can be processed when
    // needed. Since we use the async libusb interface, we do the monitoring
    // ourselves. That means we always need keep track of the libusb file
    // descriptor list.

    // Subscribe to changes to the list of file-descriptors we have to monitor.
    libusb_set_pollfd_notifiers(libusb_ctx,
            [](int fd, short events, void *user_data) {
                ((USBHostSideDiscoverer*)user_data)->pollfd_added_handler(fd, events);
            },
            [](int fd, void *user_data) {
                ((USBHostSideDiscoverer*)user_data)->pollfd_removed_handler(fd);
            }, this);

    // Fetch initial list of file-descriptors we have to monitor.
    // Note: this will fail on Windows. Since this is used for epoll, we need a
    // different approach for Windows anyway.
    pollfds = libusb_get_pollfds(libusb_ctx);
    if (pollfds) {
        for (size_t i = 0; pollfds[i]; ++i) {
            pollfd_added_handler(pollfds[i]->fd, pollfds[i]->events);
        }
        libusb_free_pollfds(pollfds);
        pollfds = nullptr;
    } else {
        fprintf(stderr, "Warning: libusb_get_pollfds() returned NULL. Probably we won't catch USB events.\n");
    }

    return 0;

fail2:
    libusb_exit(libusb_ctx);
    libusb_ctx = nullptr;
fail1:
    udev_unref(udev_ctx);
    udev_ctx = nullptr;
fail0:
    return -1;
}

int USBHostSideDiscoverer::deinit() {
    // TODO: verify that all devices are closed and hotplug detection is disabled

    // Deregister libusb events from our worker.
    libusb_set_pollfd_notifiers(libusb_ctx, nullptr, nullptr, nullptr);
    const struct libusb_pollfd** pollfds = libusb_get_pollfds(libusb_ctx);
    if (pollfds) {
        for (size_t i = 0; pollfds[i]; ++i) {
            pollfd_removed_handler(pollfds[i]->fd);
        }
        libusb_free_pollfds(pollfds);
        pollfds = nullptr;
    }

    // FIXME: the libusb_hotplug_deregister_callback call will still trigger a
    // usb_handler event. We need to wait until this has finished before we
    // truly discard libusb resources
    //usleep(100000);

    libusb_exit(libusb_ctx);
    libusb_ctx = nullptr;

    udev_unref(udev_ctx);
    udev_ctx = nullptr;
    return 0;
}


void USBHostSideDiscoverer::udev_handler() {
    printf("udev handler\n");
    struct udev_device* dev = udev_monitor_receive_device(udev_mon);
    (void) dev; // the device is not actually being used
    printf("udev handler completed\n");
}

void USBHostSideDiscoverer::usb_handler() {
    printf("usb handler\n");
    timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    if (libusb_handle_events_timeout(libusb_ctx, &tv) != 0) {
        printf("libusb_handle_events_timeout() failed\n");
    }
    printf("usb handler completed\n");
}

void USBHostSideDiscoverer::pollfd_added_handler(int fd, short events) {
    worker_->register_event(fd, events, &usb_handler_obj);
}

void USBHostSideDiscoverer::pollfd_removed_handler(int fd) {
    worker_->deregister_event(fd);
}

/**
 * @brief Called by libusb when a USB device was plugged in or out.
 * 
 * Checks if this device has any interfaces that are likely to be compatible
 * with Fibre. If so, the corresponding endpoints are registered as new Fibre
 * channels.
 */
int USBHostSideDiscoverer::hotplug_callback(struct libusb_context *ctx, struct libusb_device *dev,
                     libusb_hotplug_event event) {
    static libusb_device_handle *handle = nullptr;
    struct libusb_device_descriptor dev_desc;
    struct libusb_config_descriptor* config_desc = nullptr;
    int rc;

    printf("hotplug callback\n");
    
    if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event) {
        (void) libusb_get_device_descriptor(dev, &dev_desc); // currently not used
        if (libusb_get_active_config_descriptor(dev, &config_desc) != LIBUSB_SUCCESS) {
            printf("Failed to get active config descriptor\n");
            return 0; // ignore errors
        }
        for (uint8_t i = 0; i < config_desc->bNumInterfaces; ++i) {
            for (int j = 0; j < config_desc->interface[i].num_altsetting; ++j) {
                const struct libusb_interface_descriptor* intf_desc = &(config_desc->interface[i].altsetting[j]);
                if (intf_desc->bInterfaceClass == 1234 && intf_desc->bInterfaceSubClass == 1234 && intf_desc->bInterfaceProtocol == 0) {
                    // OPEN DEVICE AND PRESENT ENDPOINTS TO FIBRE
                    rc = libusb_open(dev, &handle);
                    if (LIBUSB_SUCCESS != rc) {
                        printf("Could not open USB device\n");
                    }
                }
            }
        }
        libusb_free_config_descriptor(config_desc);
        config_desc = nullptr;
    } else {
        printf("Unhandled event %d\n", event);
    }
    
    return 0;
}

/**
 * @brief Starts the udev monitor to monitor hotplug events of new USB devices.
 * The corresponding event is added to the epoll interest set.
 * 
 * Returns an error code if the udev monitor was already started.
 */
int USBHostSideDiscoverer::start_udev_monitor() {
    if (is_udev_monitor_started())
        return -1;

    int mon_fd = -1;

    udev_mon = udev_monitor_new_from_netlink(udev_ctx, "udev");

    if (!udev_mon) {
        printf("Error creating udev monitor\n");
        return -1;
    }
    if (udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "usb", nullptr) != 0) {
        printf("udev_monitor_filter_add_match_subsystem_devtype() failed\n");
        goto fail;
    }
    if (udev_monitor_enable_receiving(udev_mon) != 0) {
        printf("udev_monitor_enable_receiving() failed\n");
        goto fail;
    }

    mon_fd = udev_monitor_get_fd(udev_mon);
    if (!worker_ || (worker_->register_event(mon_fd, EPOLLIN, &udev_handler_obj) != 0)) {
        printf("register_event(mon_fd) failed\n");
        goto fail;
    }
    return 0;

fail:
    udev_monitor_unref(udev_mon);
    udev_mon = nullptr;
    return -1;
}

/**
 * Stops the udev monitor that was started with start_udev_monitor().
 * 
 * Returns an error code if the udev monitor was already started.
 */
int USBHostSideDiscoverer::stop_udev_monitor() {
    if (!is_udev_monitor_started())
        return -1;
    
    int result = 0;

    int mon_fd = udev_monitor_get_fd(udev_mon);
    if (!worker_ || (worker_->deregister_event(mon_fd) != 0)) {
        printf("deregister_event(mon_fd) failed\n");
        result = -1;
    }

    udev_monitor_unref(udev_mon);
    udev_mon = nullptr;

    return result;
}


/**
 * @brief Starts monitoring USB devices in a background thread.
 */
int USBHostSideDiscoverer::start_libusb_monitor() {
    if (!libusb_ctx)
        return -1;

    if (start_udev_monitor() != 0) {
        printf("Could not start udev monitor. Fall back to polling.\n");
    }

    if (!is_udev_monitor_started() && timer_.start(1000, true, nullptr) != 0) {
        printf("Could not start polling timer.\n");
        goto fail;
    }

    int result;
    result = libusb_hotplug_register_callback(libusb_ctx,
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, LIBUSB_HOTPLUG_ENUMERATE,
            LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
            [](struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data){
                return ((USBHostSideDiscoverer*)user_data)->hotplug_callback(ctx, dev, event);
            }, this, &hotplug_callback_handle);
    if (LIBUSB_SUCCESS != result) {
        printf("Error creating a hotplug callback\n");
        goto fail;
    }

    /*while (1) {
        printf("wait for USB activity...\n");
        libusb_handle_events_completed(libusb_ctx, NULL);
    }*/

    return 0;

fail:
    if (is_udev_monitor_started()) {
        stop_udev_monitor();
    }
    if (timer_.is_started()) {
        timer_.stop();
    }

    return -1;
}

int USBHostSideDiscoverer::stop_libusb_monitor() {
    int result = 0;

    libusb_hotplug_deregister_callback(libusb_ctx, hotplug_callback_handle);
    if (is_udev_monitor_started() && (stop_udev_monitor() != 0)) {
        result = -1;
    }
    if (timer_.is_started() && (timer_.stop() != 0)) {
        result = -1;
    }
    return result;
}

/**
 * @brief Starts creating channels that might help in finding the requested kind of objects.
 * 
 * If the function succeeds, an opaque context pointer is returned which must be
 * passed to stop_channel_discovery() to terminate this particular request.
 */
int USBHostSideDiscoverer::start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx) {
    // if there are already discovery requests in place, there's nothing to do
    if (!n_discovery_requests) {
        if (start_libusb_monitor() != 0) {
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
int USBHostSideDiscoverer::stop_channel_discovery(void* discovery_ctx) {
    int result = 0;
    if (n_discovery_requests == 1) {
        if (stop_libusb_monitor() != 0) {
            printf("Stop USB device discovery\n");
            result = -1;
        }
    }
    n_discovery_requests--;
    return result;
}
