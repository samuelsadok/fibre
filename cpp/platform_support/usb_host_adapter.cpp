
#include "usb_host_adapter.hpp"
#include <fibre/channel_discoverer.hpp>
#include <fibre/domain.hpp>

#if !FIBRE_ALLOW_HEAP
#error "USB Host adapter need heap memory allocation"
#endif

using namespace fibre;

struct UsbHostAdapterBulkInEndpoint final : public AsyncStreamSource {
    void start_read(bufptr_t buffer, TransferHandle* handle, Callback<void, ReadResult> completer) final {
        if (handle) {
            *handle = (TransferHandle)this;
        }
        completer_ = completer;
        device_->bulk_in_transfer(ep_num_, buffer, MEMBER_CB(this, on_transfer_finished));
    }

    void cancel_read(TransferHandle transfer_handle) final {
        // TODO
    }

    void on_transfer_finished(RichStatus status, unsigned char* end) {
        if (status.is_success()) {
            completer_.invoke_and_clear({kStreamOk, end});
        } else { // TODO: log error
            completer_.invoke_and_clear({kStreamError, end});
        }
    }

    int ep_num_;
    UsbDevice* device_;
    Callback<void, ReadResult> completer_;
};

struct UsbHostAdapterBulkOutEndpoint final : public AsyncStreamSink {
    void start_write(cbufptr_t buffer, TransferHandle* handle, Callback<void, WriteResult0> completer) final {
        if (handle) {
            *handle = (TransferHandle)this;
        }
        completer_ = completer;
        device_->bulk_out_transfer(ep_num_, buffer, MEMBER_CB(this, on_transfer_finished));
    }

    void cancel_write(TransferHandle transfer_handle) final {
        // TODO
    }

    void on_transfer_finished(RichStatus status, const unsigned char* end) {
        if (status.is_success()) {
            completer_.invoke_and_clear({kStreamOk, end});
        } else { // TODO: log error
            completer_.invoke_and_clear({kStreamError, end});
        }
    }

    int ep_num_;
    UsbDevice* device_;
    Callback<void, WriteResult0> completer_;
};

struct fibre::OpenDevice {
    uint8_t interface_num;
    uint16_t mtu;
    UsbHostAdapterBulkInEndpoint ep_in;
    UsbHostAdapterBulkOutEndpoint ep_out;
};

void UsbHostAdapter::start(Domain* domain, const char* specs, size_t specs_len) {
    F_LOG_D(logger_, "starting");
    domain_ = domain;

    InterfaceSpecs interface_specs;

    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "bus", &interface_specs.bus);
    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "address", &interface_specs.address);
    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "idVendor", &interface_specs.vendor_id);
    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "idProduct", &interface_specs.product_id);
    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "bInterfaceClass", &interface_specs.interface_class);
    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "bInterfaceSubClass", &interface_specs.interface_subclass);
    ChannelDiscoverer::try_parse_key(specs, specs + specs_len, "bInterfaceProtocol", &interface_specs.interface_protocol);

    specs_ = interface_specs;

    usb_->start(MEMBER_CB(this, on_found_device), MEMBER_CB(this, on_lost_device));
}

void UsbHostAdapter::stop() {
    F_LOG_D(logger_, "starting");
    usb_->stop();
}

RichStatus UsbHostAdapter::show_device_dialog() {
    return usb_->request_device(
        specs_.vendor_id != -1 ? std::make_optional<uint16_t>(specs_.vendor_id) : std::nullopt,
        specs_.product_id != -1 ? std::make_optional<uint16_t>(specs_.product_id) : std::nullopt,
        specs_.interface_class != -1 ? std::make_optional<uint8_t>(specs_.interface_class) : std::nullopt,
        specs_.interface_subclass != -1 ? std::make_optional<uint8_t>(specs_.interface_subclass) : std::nullopt,
        specs_.interface_protocol != -1 ? std::make_optional<uint8_t>(specs_.interface_protocol) : std::nullopt
    );
}

RichStatus UsbHostAdapter::consider(UsbDevice* device, InterfaceSpecs* specs) {
    uint8_t bus;
    uint8_t address;
    uint16_t vendor_id;
    uint16_t product_id;

    F_RET_IF_ERR(device->get_info(
        specs->bus == -1 ? nullptr : &bus,
        specs->address == -1 ? nullptr : &address,
        specs->vendor_id == -1 ? nullptr : &vendor_id,
        specs->product_id == -1 ? nullptr : &product_id
    ), "failed to get device info");

    bool mismatch = (specs->bus != -1 && bus != specs->bus)
                 || (specs->address != -1 && address != specs->address)
                 || (specs->vendor_id != -1 && vendor_id != specs->vendor_id)
                 || (specs->product_id != -1 && product_id != specs->product_id);

    if (mismatch) {
        F_LOG_D(logger_, "ignoring device due to filter " << vendor_id << " " << product_id << " " << specs->vendor_id << " " << specs->product_id);
        return RichStatus::success();
    }

    bool match = false;
    OpenDevice dev;
    dev.ep_in.device_ = device;
    dev.ep_out.device_ = device;

    F_RET_IF_ERR(device->with_active_config_desc([&](const UsbConfigDesc* config_desc) {
        for (uint8_t i = 0; i < config_desc->n_interfaces; ++i) {
            for (size_t j = 0; j < config_desc->interfaces[i].n_altsettings; ++j) {
                const UsbAlternateDesc* alt_desc = &(config_desc->interfaces[i].alternates[j]);

                bool mismatch = (specs->interface_class != -1 && alt_desc->interface_class != specs->interface_class)
                             || (specs->interface_subclass != -1 && alt_desc->interface_subclass != specs->interface_subclass)
                             || (specs->interface_protocol != -1 && alt_desc->interface_protocol != specs->interface_protocol);
                if (mismatch) {
                    continue;
                }

                for (uint8_t k = 0; k < alt_desc->n_endpoints; ++k) {
                    const UsbEndpointDesc* ep_desc = &alt_desc->endpoints[k];
                    bool is_in = ep_desc->number & 0x80;
                    if (ep_desc->type == UsbTransferType::kBulk && is_in) {
                        dev.ep_in.ep_num_ = ep_desc->number;
                    } else if (ep_desc->type == UsbTransferType::kBulk && !is_in) {
                        dev.ep_out.ep_num_ = ep_desc->number;
                        dev.mtu = ep_desc->max_packet_size;
                    }
                }

                if (dev.ep_in.ep_num_ != 0 && dev.ep_out.ep_num_ != 0) {
                    F_LOG_D(logger_, "found matching interface with mtu " << dev.mtu);
                    match = true;
                    dev.interface_num = i;
                    return;
                }
            }
        }
    }), "can't get active config");

    if (match) {
        F_LOG_D(logger_, "this device is good");
        open_devices_[device] = new OpenDevice{dev};
        device->open(MEMBER_CB(this, on_opened_device));
    }

    return RichStatus::success();
}

void UsbHostAdapter::on_found_device(UsbDevice* device) {
    F_LOG_D(logger_, "found device");
    F_LOG_IF_ERR(logger_, consider(device, &specs_), "failed to check device");
}

void UsbHostAdapter::on_lost_device(UsbDevice* device) {
    F_LOG_D(logger_, "lost device");
    auto it = open_devices_.find(device);
    OpenDevice* dev = it->second;
    if (dev->ep_in.completer_.has_value() || dev->ep_out.completer_.has_value()) {
        F_LOG_E(logger_, "Device removed before transfer was finished. Leaking memory.");
    } else {
        delete dev;
        open_devices_.erase(it);
    }
}

void UsbHostAdapter::on_opened_device(RichStatus status, UsbDevice* device) {
    OpenDevice* dev = open_devices_[device];

    if (F_LOG_IF_ERR(logger_, status, "couldn't open device")) {
        delete dev;
        open_devices_.erase(device);
        return;
    }

    device->claim_interface(dev->interface_num, MEMBER_CB(this, on_claimed_interface));
}

void UsbHostAdapter::on_claimed_interface(RichStatus status, UsbDevice* device) {
    OpenDevice* dev = open_devices_[device];

    if (F_LOG_IF_ERR(logger_, status, "couldn't claim interface " << dev->interface_num)) {
        delete dev;
        open_devices_.erase(device);
        return;
    }

    domain_->add_legacy_channels({kFibreOk, &dev->ep_in, &dev->ep_out, dev->mtu, true}, "USB");
}
