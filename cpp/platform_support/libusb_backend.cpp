/**
 * @brief Backend: libusb
 * 
 * Platform Compatibility: Linux, Windows, macOS
 */

#include "libusb_backend.hpp"

#if FIBRE_ENABLE_LIBUSB_BACKEND

#include "usb_host_adapter.hpp"
#include "../print_utils.hpp"
//#include <fibre/fibre.hpp>
#include <fibre/event_loop.hpp>

#include <libusb.h>
#include <algorithm>
#include <string.h>

#if !FIBRE_ALLOW_HEAP
#  error "The libusb backend requires heap allocation."
#endif

using namespace fibre;

// This probably has no noteworthy effect since we automatically restart
// timed out operations anyway.
constexpr unsigned int kBulkTimeoutMs = 10000;

// Only relevant for platforms don't support hotplug detection and thus
// need polling.
constexpr unsigned int kPollingIntervalMs = 1000;



#define USE_SEPARATE_THREAD 0

#if USE_SEPARATE_THREAD
#include <thread>
#endif


namespace fibre {

struct LibUsbDevice;

struct LibUsbTransfer {
    LibUsbTransfer(LibUsbDevice* device) : device(device), handle(libusb_alloc_transfer(0)) {}
    ~LibUsbTransfer();
    LibUsbDevice* device;
    struct libusb_transfer* handle;
};

struct BulkInTransfer : LibUsbTransfer {
    BulkInTransfer(LibUsbDevice* device) : LibUsbTransfer{device} {}
    void on_transfer_finished();
    bufptr_t buffer;
    Callback<void, RichStatus, unsigned char*> callback;
};

struct BulkOutTransfer : LibUsbTransfer {
    BulkOutTransfer(LibUsbDevice* device) : LibUsbTransfer{device} {}
    void on_transfer_finished();
    cbufptr_t buffer;
    Callback<void, RichStatus, const unsigned char*> callback;
};

struct LibUsbDevice final : public UsbDevice {
    LibUsbDevice(LibUsb* libusb, struct libusb_device* dev);
    ~LibUsbDevice();

    RichStatus get_info(uint8_t* bus, uint8_t* address, uint16_t* vendor_id, uint16_t* product_id) final;
    RichStatus with_active_config_desc(Callback<void, UsbConfigDesc*> callback) final;
    RichStatus open(Callback<void, RichStatus, UsbDevice*> callback) final;
    RichStatus claim_interface(uint8_t interface_num, Callback<void, RichStatus, UsbDevice*> callback) final;
    RichStatus bulk_in_transfer(uint8_t ep_num, bufptr_t buffer, Callback<void, RichStatus, unsigned char*> callback) final;
    RichStatus bulk_out_transfer(uint8_t ep_num, cbufptr_t buffer, Callback<void, RichStatus, const unsigned char*> callback) final;

    template<typename TTransfer, typename TBuf, typename TCb>
    RichStatus transfer(uint8_t ep_num, TBuf buffer, TCb callback, std::vector<TTransfer*>& transfer_cache);

    LibUsb* libusb_;
    struct libusb_device* dev_;
    struct libusb_device_handle* handle_;
    std::vector<BulkInTransfer*> bulk_in_transfers_;
    std::vector<BulkOutTransfer*> bulk_out_transfers_;
};

struct LibUsb final : public UsbHostController {
    LibUsb(Logger logger) : logger_(logger) {}

    RichStatus start(on_found_device_t on_found,
                     on_lost_device_t on_lost) final;
    RichStatus stop() final;
    RichStatus request_device(std::optional<uint16_t> vendor_id, std::optional<uint16_t> product_id, std::optional<uint8_t> intf_class, std::optional<uint8_t> intf_subclass, std::optional<uint8_t> intf_protocol) final;

#if USE_SEPARATE_THREAD
    void internal_event_loop();
#else
    void on_event_loop_iteration();
    void on_event_loop_iteration2(uint32_t) { on_event_loop_iteration(); }
    void on_add_pollfd(int fd, short events);
    void on_remove_pollfd(int fd);
#endif

    int on_hotplug(struct libusb_device *dev, libusb_hotplug_event event);
    void poll_devices_now();

    Logger logger_;
    EventLoop* event_loop_;
    on_found_device_t on_found_;
    on_lost_device_t on_lost_;
    libusb_context *libusb_ctx_ = nullptr; // libusb session
    libusb_hotplug_callback_handle hotplug_callback_handle_ = 0;
    Timer* device_polling_timer_ = nullptr;
    std::unordered_map<uint16_t, LibUsbDevice*> known_devices_; // key: bus_number << 8 | dev_number

#if USE_SEPARATE_THREAD
    bool run_internal_event_loop_ = false;
    std::thread* internal_event_loop_thread_ = nullptr;
#else
    Timer* event_loop_timer_ = nullptr;
#endif
};

}


/* LibUsb --------------------------------------------------------------------*/

/**
 * @brief Initializes the discoverer.
 * 
 * Asynchronous tasks will be executed on the provided event_loop.
 * 
 * @param event_loop: The event loop that is used to execute background tasks. The
 *        pointer must be non-null and initialized when this function is called.
 *        It must remain initialized until deinit() of this discoverer was called.
 */
RichStatus LibUsb::start(on_found_device_t on_found, on_lost_device_t on_lost) {
    F_RET_IF(!event_loop_, "invalid argument");
    on_found_ = on_found;
    on_lost_ = on_found;

    if (libusb_init(&libusb_ctx_) != LIBUSB_SUCCESS) {
        libusb_ctx_ = nullptr;
        return F_MAKE_ERR("libusb_init() failed: " << sys_err());
    }

    // Fetch initial list of file-descriptors we have to monitor.
    // Note: this will return NULL on Windows, however we still do it to notice
    // bad compile settings.
    const struct libusb_pollfd** pollfds = libusb_get_pollfds(libusb_ctx_);

#if USE_SEPARATE_THREAD
    // This code path is taken on Windows
    F_LOG_D(logger_, "Using separate event loop thread");

    if (pollfds) {
        F_LOG_W(logger_, "Spawning separate thread even though libusb could integrate with the event loop.");
    }

    // This code path is taken on Windows (which does not support epoll)
    run_internal_event_loop_ = true;
    internal_event_loop_thread_ = new std::thread([](void* ctx) {
        ((LibUsb*)ctx)->internal_event_loop();
    }, this);

#else
    // This code path is taken on Linux and macOS
    F_LOG_D(logger_, "Running libusb on Fibre's event loop");

    if (!pollfds) {
        stop();
        return F_MAKE_ERR("libusb_get_pollfds() failed");
    }

    for (size_t i = 0; pollfds[i]; ++i) {
        on_add_pollfd(pollfds[i]->fd, pollfds[i]->events);
    }
    libusb_free_pollfds(pollfds);
    pollfds = nullptr;

    // libusb maintains a (dynamic) list of file descriptors that need to be
    // monitored (via select/poll/epoll) so that I/O events can be processed when
    // needed. Since we use the async libusb interface, we do the monitoring
    // ourselves. That means we always need keep track of the libusb file
    // descriptor list.

    // Subscribe to changes to the list of file-descriptors we have to monitor.
    libusb_set_pollfd_notifiers(libusb_ctx_,
            [](int fd, short events, void *user_data) {
                ((LibUsb*)user_data)->on_add_pollfd(fd, events);
            },
            [](int fd, void *user_data) {
                ((LibUsb*)user_data)->on_remove_pollfd(fd);
            }, this);

    // Check if libusb needs time-based polling on this platform (for diagnostics only)
    if (libusb_pollfds_handle_timeouts(libusb_ctx_) == 0) {
        F_LOG_D(logger_, "Using time-based polling");
    }

    RichStatus status = event_loop_->open_timer(&event_loop_timer_, MEMBER_CB(this, on_event_loop_iteration));
    if (status.is_error()) {
        event_loop_timer_ = nullptr;
        stop();
        return status;
    }
#endif


    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        // This code path is taken on Linux
        F_LOG_D(logger_, "Using libusb native hotplug detection");

        // Subscribe to hotplug events
        int result = libusb_hotplug_register_callback(libusb_ctx_,
                             (libusb_hotplug_event)(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
                             LIBUSB_HOTPLUG_ENUMERATE /* trigger callback for all currently connected devices too */,
                             LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
                             [](struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data){
                                 return ((LibUsb*)user_data)->on_hotplug(dev, event);
                             }, this, &hotplug_callback_handle_);

        // TODO: Not sure if hotplug_callback_handle_ is guaranteed to be
        // non-zero on success, but we rely on that assumption in stop() so we
        // better check it.
        if (LIBUSB_SUCCESS != result || hotplug_callback_handle_ == 0) {
            hotplug_callback_handle_ = 0;
            stop();
            return F_MAKE_ERR("Error subscribing to hotplug events");
        }

    } else {
        // This code path is taken on Windows
        F_LOG_D(logger_, "Using periodic polling to discover devices");

        RichStatus status = event_loop_->open_timer(&device_polling_timer_, MEMBER_CB(this, poll_devices_now));
        if (status.is_error()) {
            device_polling_timer_ = nullptr;
            stop();
            return status;
        }
        device_polling_timer_->set(kPollingIntervalMs * 0.001f, TimerMode::kPeriodic);
        poll_devices_now();
    }

    // The hotplug callback handler above is not yet thread-safe. To make it thread-safe
    // we'd need to post it on the application's event loop.
    F_LOG_IF(logger_,
             !pollfds && libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG),
             "Hotplug detection with separate libusb thread will cause trouble.");

    return RichStatus::success();
}

RichStatus LibUsb::stop() {
    // TODO: verify that all devices are closed and hotplug detection is disabled

    if (hotplug_callback_handle_) {
        libusb_hotplug_deregister_callback(libusb_ctx_, hotplug_callback_handle_);
    }
    
    if (device_polling_timer_) {
        event_loop_->close_timer(device_polling_timer_);
        device_polling_timer_ = nullptr;
    }



#if USE_SEPARATE_THREAD
    run_internal_event_loop_ = false;
    if (internal_event_loop_thread_) {
        libusb_interrupt_event_handler(libusb_ctx_);
        internal_event_loop_thread_->join();
        delete internal_event_loop_thread_;
        internal_event_loop_thread_ = nullptr;
    }
#else
    if (libusb_ctx_) {
        // Deregister libusb events from our event loop.
        const struct libusb_pollfd** pollfds = libusb_get_pollfds(libusb_ctx_);
        if (pollfds) {
            for (size_t i = 0; pollfds[i]; ++i) {
                on_remove_pollfd(pollfds[i]->fd);
            }
            libusb_free_pollfds(pollfds);
            pollfds = nullptr;
        }

        libusb_set_pollfd_notifiers(libusb_ctx_, nullptr, nullptr, nullptr);
    }

    if (event_loop_timer_) {
        event_loop_->close_timer(event_loop_timer_);
        event_loop_timer_ = nullptr;
    }
#endif

    // TODO: we should probably deinit and close all connected channels
    for (auto& dev: known_devices_) {
        delete dev.second;
    }

    if (libusb_ctx_) {
        libusb_exit(libusb_ctx_);
        libusb_ctx_ = nullptr;
    }

    on_found_.clear();
    on_lost_.clear();

    return RichStatus::success();
}

RichStatus LibUsb::request_device(std::optional<uint16_t> vendor_id, std::optional<uint16_t> product_id, std::optional<uint8_t> intf_class, std::optional<uint8_t> intf_subclass, std::optional<uint8_t> intf_protocol) {
    return F_MAKE_ERR("not supported");
}

#if USE_SEPARATE_THREAD

/**
 * @brief Runs the event handling loop. This function blocks until
 * run_internal_event_loop_ is false.
 * 
 * This loop is only executed on Windows. On other platforms the provided EventLoop is used.
 */
void LibUsb::internal_event_loop() {
    while (run_internal_event_loop_)
        libusb_handle_events(libusb_ctx_);
}

#else

void LibUsb::on_event_loop_iteration() {
    F_LOG_IF_ERR(logger_, event_loop_timer_->set(0.0f, TimerMode::kNever), "failed to set timer");

    timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    F_LOG_IF(logger_, libusb_handle_events_timeout(libusb_ctx_, &tv) != 0,
             "libusb_handle_events_timeout() failed");

    timeval timeout;
    if (libusb_get_next_timeout(libusb_ctx_, &timeout)) {
        float timeout_sec = (float)timeout.tv_sec + (float)timeout.tv_usec * 1e-6;
        F_LOG_D(logger_, "setting event loop timeout to " << timeout_sec << " s");
        F_LOG_IF_ERR(logger_, event_loop_timer_->set(timeout_sec, TimerMode::kOnce),
            "failed to set timer");
    }
}

/**
 * @brief Called when libusb wants to add a file descriptor to our event loop.
 */
void LibUsb::on_add_pollfd(int fd, short events) {
    event_loop_->register_event(fd, events,
        MEMBER_CB(this, on_event_loop_iteration2));
}

/**
 * @brief Called when libusb wants to remove a file descriptor to our event loop.
 */
void LibUsb::on_remove_pollfd(int fd) {
    event_loop_->deregister_event(fd);
}

#endif

/**
 * @brief Called by libusb when a USB device was plugged in or out.
 *
 * If this function returns a non-zero value, libusb removes this filter.
 */
int LibUsb::on_hotplug(struct libusb_device *dev,
                                 libusb_hotplug_event event) {
    uint8_t bus_number = libusb_get_bus_number(dev);
    uint8_t dev_number = libusb_get_device_address(dev);
    
    if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event) {
        F_LOG_D(logger_, "device arrived: bus " << (int)bus_number << ", " << (int)dev_number);

        LibUsbDevice* device = new LibUsbDevice{this, dev};
        known_devices_[bus_number << 8 | dev_number] = device;
        on_found_.invoke(device);

    } else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event) {
        F_LOG_D(logger_, "device left: bus " << (int)bus_number << ", " << (int)dev_number);

        auto it = known_devices_.find(bus_number << 8 | dev_number);

        if (it != known_devices_.end()) {
            on_lost_.invoke(it->second);
            delete it->second; // this will free the allocated transfers and close and and release the handle
            known_devices_.erase(it);
        }

    } else {
        F_LOG_E(logger_, "Unexpected event: " << event);
    }
    
    return 0;
}

void LibUsb::poll_devices_now() {
    F_LOG_D(logger_, "poll_devices_now() called.");

    device_polling_timer_ = nullptr;

    libusb_device** list = nullptr;
    ssize_t n_devices = libusb_get_device_list(libusb_ctx_, &list);
    std::unordered_map<uint16_t, libusb_device*> current_devices;

    if (n_devices < 0) {
        F_LOG_E(logger_, "libusb_get_device_list() failed.");
    } else {
        for (ssize_t i = 0; i < n_devices; ++i) {
            uint8_t bus_number = libusb_get_bus_number(list[i]);
            uint8_t dev_number = libusb_get_device_address(list[i]);
            current_devices[bus_number << 8 | dev_number] = list[i];
        }

        // Call on_hotplug for all new devices
        for (auto& dev: current_devices) {
            if (known_devices_.find(dev.first) == known_devices_.end()) {
                on_hotplug(dev.second, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);

                // Immediately forget about the devices that weren't opened on plugin.
                // The reason is this: On Windows the device address and even the
                // device pointer can remain equal across device reset. Since we don't
                // poll at infinite frequency This means we could miss a device reset.
                // To avoid this, we reinspect the all unopened devices on
                // every polling iteration.
                auto it = known_devices_.find(dev.first);
                if (it->second->handle_ == nullptr) {
                    delete it->second;
                    known_devices_.erase(it);
                }
            }
        }

        // Call on_hotplug for all lost devices

        std::vector<libusb_device*> lost_devices;

        for (auto& dev: known_devices_) {
            if (current_devices.find(dev.first) == current_devices.end()) {
                lost_devices.push_back(dev.second->dev_);
            }
        }

        for (auto& dev: lost_devices) {
            on_hotplug(dev, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);
        }

        libusb_free_device_list(list, 1 /* unref the devices */);
    }
}

LibUsbDevice::LibUsbDevice(LibUsb* libusb, struct libusb_device* dev) :
    libusb_(libusb),
    dev_(libusb_ref_device(dev)) {}

LibUsbDevice::~LibUsbDevice() {
    for (auto transfer : bulk_in_transfers_) {
        if (transfer->callback.has_value()) {
            F_LOG_E(libusb_->logger_, "Transfer on EP " << as_hex(transfer->handle->endpoint) << " still in progress. This is gonna be messy.");
        }
        delete transfer;
    }

    for (auto transfer : bulk_in_transfers_) {
        if (transfer->callback.has_value()) {
            F_LOG_E(libusb_->logger_, "Transfer on EP " << as_hex(transfer->handle->endpoint) << " still in progress. This is gonna be messy.");
        }
        delete transfer;
    }
    
    if (handle_) {
        libusb_close(handle_);
        handle_ = nullptr;
    }
    libusb_unref_device(dev_);
    dev_ = nullptr;
}

RichStatus LibUsbDevice::get_info(uint8_t* bus, uint8_t* address, uint16_t* vendor_id, uint16_t* product_id) {
    if (bus) {
        *bus = libusb_get_bus_number(dev_);
    }
    if (address) {
        *address = libusb_get_device_address(dev_);
    }

    if (vendor_id || product_id) {
        struct libusb_device_descriptor dev_desc;
        int result = libusb_get_device_descriptor(dev_, &dev_desc);
        F_RET_IF(result != LIBUSB_SUCCESS, "Failed to get device descriptor: " << result);

        if (vendor_id) {
            *vendor_id = dev_desc.idVendor;
        }
        if (product_id) {
            *product_id = dev_desc.idProduct;
        }
    }

    return RichStatus::success();
}

RichStatus LibUsbDevice::with_active_config_desc(Callback<void, UsbConfigDesc*> callback) {
    struct libusb_config_descriptor* config_desc_src = nullptr;

    int result = libusb_get_active_config_descriptor(dev_, &config_desc_src);
    F_RET_IF(result != LIBUSB_SUCCESS, "Failed to get active config descriptor: " << result);

    std::vector<UsbInterfaceDesc> intf_descs{config_desc_src->bNumInterfaces};
    std::vector<std::vector<UsbAlternateDesc>> alt_descs{config_desc_src->bNumInterfaces};
    std::vector<std::vector<UsbEndpointDesc>> ep_descs{};
    
    for (uint8_t i = 0; i < config_desc_src->bNumInterfaces; ++i) {
        alt_descs[i] = std::vector<UsbAlternateDesc>{(size_t)config_desc_src->interface[i].num_altsetting};

        for (int j = 0; j < config_desc_src->interface[i].num_altsetting; ++j) {
            const struct libusb_interface_descriptor* alt_desc = &(config_desc_src->interface[i].altsetting[j]);

            alt_descs[i][j].interface_class = alt_desc->bInterfaceClass;
            alt_descs[i][j].interface_subclass = alt_desc->bInterfaceSubClass;
            alt_descs[i][j].interface_protocol = alt_desc->bInterfaceProtocol;

            ep_descs.push_back(std::vector<UsbEndpointDesc>{alt_desc->bNumEndpoints});

            for (uint8_t k = 0; k < alt_desc->bNumEndpoints; ++k) {
                UsbEndpointDesc& ep_desc_dst = ep_descs.back()[k];

                uint8_t transfer_type = alt_desc->endpoint[k].bmAttributes & 0x03;
                switch (transfer_type) {
                    case LIBUSB_TRANSFER_TYPE_CONTROL: ep_desc_dst.type = UsbTransferType::kControl; break;
                    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS: ep_desc_dst.type = UsbTransferType::kIsochronous; break;
                    case LIBUSB_TRANSFER_TYPE_BULK: ep_desc_dst.type = UsbTransferType::kBulk; break;
                    case LIBUSB_TRANSFER_TYPE_INTERRUPT: ep_desc_dst.type = UsbTransferType::kInterrupt; break;
                    case LIBUSB_TRANSFER_TYPE_BULK_STREAM: ep_desc_dst.type = UsbTransferType::kBulkStream; break;
                    default:
                        return F_MAKE_ERR("unknown transfer type: " << transfer_type);
                }

                ep_desc_dst.number = alt_desc->endpoint[k].bEndpointAddress;
                ep_desc_dst.max_packet_size = alt_desc->endpoint[k].wMaxPacketSize;
            }

            alt_descs[i][j].endpoints = ep_descs.back().data();
            alt_descs[i][j].n_endpoints = ep_descs.back().size();
        }

        intf_descs[i] = UsbInterfaceDesc{
            alt_descs[i].size(),
            alt_descs[i].data()
        };
    }

    UsbConfigDesc config_desc_dst{
        (uint8_t)intf_descs.size(),
        intf_descs.data()
    };

    libusb_free_config_descriptor(config_desc_src);
    config_desc_src = nullptr;

    callback.invoke(&config_desc_dst);

    return RichStatus::success();
}

RichStatus LibUsbDevice::open(Callback<void, RichStatus, UsbDevice*> callback) {
    F_RET_IF(handle_, "device was already opened");

    int result = libusb_open(dev_, &handle_);
    F_RET_IF(LIBUSB_SUCCESS != result, "Could not open USB device: " << result);

    callback.invoke(RichStatus::success(), this);
    return RichStatus::success();
}

RichStatus LibUsbDevice::claim_interface(uint8_t interface_num, Callback<void, RichStatus, UsbDevice*> callback) {
    int result = libusb_claim_interface(handle_, interface_num);
    F_RET_IF(LIBUSB_SUCCESS != result, "Could not claim interface " << interface_num << " on USB device: " << result);

    callback.invoke(RichStatus::success(), this);
    return RichStatus::success();
}

RichStatus LibUsbDevice::bulk_in_transfer(uint8_t ep_num, bufptr_t buffer, Callback<void, RichStatus, unsigned char*> callback) {
    return transfer(ep_num, buffer, callback, bulk_in_transfers_);
}

RichStatus LibUsbDevice::bulk_out_transfer(uint8_t ep_num, cbufptr_t buffer, Callback<void, RichStatus, const unsigned char*> callback) {
    return transfer(ep_num, buffer, callback, bulk_out_transfers_);
}

template<typename TTransfer, typename TBuf, typename TCb>
RichStatus LibUsbDevice::transfer(uint8_t ep_num, TBuf buffer, TCb callback, std::vector<TTransfer*>& transfer_cache) {
    F_RET_IF(!handle_, "device not open");

    auto it = std::find_if(transfer_cache.begin(), transfer_cache.end(), [](TTransfer* t) { return !t->callback.has_value(); });
    if (it == transfer_cache.end()) {
        transfer_cache.push_back(new TTransfer{this});
        it = transfer_cache.end() - 1;

        auto transfer_callback = [](struct libusb_transfer* t){
            TTransfer* transfer = (TTransfer*)t->user_data;
#if USE_SEPARATE_THREAD
            transfer->device->libusb_->event_loop_->post(MEMBER_CB(transfer, on_transfer_finished));
#else
            transfer->on_transfer_finished();
#endif
        };

        libusb_fill_bulk_transfer((*it)->handle, handle_, 0,
            nullptr, 0,
            transfer_callback,
            &*it, kBulkTimeoutMs);
    }

    (*it)->handle->endpoint = ep_num;
    (*it)->handle->buffer = (unsigned char *)buffer.begin(); // libusb wants non-const buffers even for output transfers but I'm gonna blindly assume that casting is ok.
    (*it)->handle->length = buffer.size();

    int result = libusb_submit_transfer((*it)->handle);
    F_RET_IF(LIBUSB_SUCCESS != result, "couldn't start USB transfer on EP " << as_hex(ep_num) << ": " << libusb_error_name(result));

    F_LOG_T(libusb_->logger_, "started USB transfer on EP " << as_hex(ep_num));

    return RichStatus::success();
}

template<typename TTransfer>
void complete_transfer(TTransfer* transfer) {
    // TODO: The error that we get on device removal tends to be inaccurate.
    // Sometimes it's LIBUSB_TRANSFER_STALL, sometimes LIBUSB_TRANSFER_ERROR.
    // The device will still be in the list of libusb_get_device_list until all
    // transfers are terminated with an error.
    // Ideally we should find a way to distunguish an error due to removal and
    // actual errors.

    uint8_t* end = std::max(transfer->handle->buffer + transfer->handle->actual_length, transfer->handle->buffer);

    RichStatus status;
    if (transfer->handle->status != LIBUSB_TRANSFER_COMPLETED) {
        status = F_MAKE_ERR("transfer finished with " << libusb_error_name(transfer->handle->status));
    }

    bool removed = transfer->handle->status == LIBUSB_TRANSFER_NO_DEVICE;

    transfer->callback.invoke_and_clear(status, end);
    
    // If libusb does hotplug detection itself then we don't need to handle
    // device removal here. Libusb will call the corresponding hotplug callback.
    if (removed && !transfer->device->libusb_->hotplug_callback_handle_) {
#if !USE_SEPARATE_THREAD
        F_LOG_E(transfer->device->libusb_->logger_, "It's not a good idea to unref the device from within this callback. This will probably hang.");
#endif
        transfer->device->libusb_->on_hotplug(transfer->device->dev_, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);
    }
}

LibUsbTransfer::~LibUsbTransfer() {
    libusb_free_transfer(handle);
    handle = nullptr;
}

void BulkInTransfer::on_transfer_finished() {
    uint8_t* end = std::max(handle->buffer + handle->actual_length, handle->buffer);

    // We ignore timeouts here and just retry.
    // TODO: allow cancellation through a cancel_transfer() function.
    if (handle->status == LIBUSB_TRANSFER_TIMED_OUT) {
        int result = libusb_submit_transfer(handle);
        if (LIBUSB_SUCCESS != result) {
            RichStatus status = F_MAKE_ERR("couldn't restart USB transfer on EP " << as_hex(handle->endpoint) << ": " << libusb_error_name(result));
            callback.invoke_and_clear(status, end);
        }
        return;
    }
    
    complete_transfer(this);
}

void BulkOutTransfer::on_transfer_finished() {
    complete_transfer(this);
}


RichStatus LibUsbBackend::init(EventLoop* event_loop, Logger logger) {
    logger_ = logger;

    libusb_ = new LibUsb(logger_);
    adapter_ = new UsbHostAdapter(logger_, libusb_);

    F_LOG_D(logger_, "init webusb backend");

    return RichStatus::success();
}

RichStatus LibUsbBackend::deinit() {
    adapter_->stop();
    delete adapter_;
    delete libusb_;
    return RichStatus::success();
}

void LibUsbBackend::start_channel_discovery(Domain* domain, const char* specs,
                                            size_t specs_len,
                                            ChannelDiscoveryContext** handle) {
    // TODO
    adapter_->start(domain, specs, specs_len);
}

RichStatus LibUsbBackend::stop_channel_discovery(
    ChannelDiscoveryContext* handle) {
    // TODO
    return RichStatus::success();
}

#endif
