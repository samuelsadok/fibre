
#include "webusb_backend.hpp"

#if FIBRE_ENABLE_WEBUSB_BACKEND

#include "usb_host_adapter.hpp"
#include <fibre/logging.hpp>
#include <fibre/rich_status.hpp>
#include <unordered_map>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace fibre;

static inline JsObjectRef js_get_root() {
    return std::make_shared<JsObjectTempRef>(0U);
}

namespace fibre {

struct WebUsbDevice;

class WebUsb final : public UsbHostController {
public:
    WebUsb(Logger logger) : logger_(logger) {}

    RichStatus start(on_found_device_t on_found,
                     on_lost_device_t on_lost) final;
    RichStatus stop() final;

    void on_get_devices_finished(const JsStub& stub);
    void on_connect(const JsStub* args, size_t n_args);
    void on_disconnect(const JsStub* args, size_t n_args);

    Logger logger_;
    JsObjectRef usb_;
    on_found_device_t on_found_;
    on_lost_device_t on_lost_;
    std::unordered_map<unsigned int, WebUsbDevice*> known_devices_;
};

struct WebUsbDevice final : UsbDevice {
    WebUsbDevice(WebUsb* webusb, JsObjectRef ref) : webusb_(webusb), ref_(ref) {}

    RichStatus get_info(uint8_t* bus, uint8_t* address, uint16_t* vendor_id, uint16_t* product_id) final;
    RichStatus with_active_config_desc(Callback<void, UsbConfigDesc*> callback) final;
    RichStatus open(Callback<void, UsbDevice*> callback) final;
    RichStatus claim_interface(uint8_t interface_num, Callback<void, UsbDevice*> callback) final;
    RichStatus bulk_in_transfer(uint8_t ep_num, bufptr_t buffer, Callback<void, RichStatus, unsigned char*> callback) final;
    RichStatus bulk_out_transfer(uint8_t ep_num, cbufptr_t buffer, Callback<void, RichStatus, const unsigned char*> callback) final;

    void on_open_finished(const JsStub& stub);
    void on_claim_interface_finished(const JsStub& stub);
    void on_bulk_in_transfer_finished(const JsStub& stub);
    void on_bulk_out_transfer_finished(const JsStub& stub);

    WebUsb* webusb_;
    JsObjectRef ref_;
    Callback<void, UsbDevice*> open_cb_;
    Callback<void, UsbDevice*> claim_interface_cb_;
    bufptr_t bulk_in_transfer_buf_;
    Callback<void, RichStatus, unsigned char*> bulk_in_transfer_cb_;
    cbufptr_t bulk_out_transfer_buf_;
    Callback<void, RichStatus, const unsigned char*> bulk_out_transfer_cb_;
};

}  // namespace fibre

RichStatus WebUsb::start(on_found_device_t on_found, on_lost_device_t on_lost) {
    on_found_ = on_found;
    on_lost_ = on_lost;

    JsObjectRef navigator;
    F_RET_IF_ERR(js_get_root()->get_property<JsObjectRef>("navigator", &navigator), "failed to get navigator object");
    F_RET_IF_ERR(navigator->get_property<JsObjectRef>("usb", &usb_), "failed to get WebUSB object (probably not supported by this browser)");

    usb_->set_property("onconnect", MEMBER_CB(this, on_connect));
    usb_->set_property("ondisconnect", MEMBER_CB(this, on_disconnect));

    std::unordered_map<
        std::string, std::vector<std::unordered_map<std::string, std::string>>>
        filters = {{"filters", {}}};

    usb_->call_async("getDevices", MEMBER_CB(this, on_get_devices_finished), 0,
                     filters);

    return RichStatus::success();
}

RichStatus WebUsb::stop() {
    // TODO: cancel getDevices

    usb_->set_property("onconnect", js_undefined);
    usb_->set_property("ondisconnect", js_undefined);

    on_found_ = {};
    on_lost_ = {};

    return RichStatus::success();
}

void WebUsb::on_get_devices_finished(const JsStub& stub) {
    F_LOG_D(logger_, "got devices");

    std::vector<JsObjectRef> devices;
    if (F_LOG_IF_ERR(logger_, from_js(stub, &devices), "in device list")) {
        return;
    }

    F_LOG_D(logger_, "got " << devices.size() << " devices");

    for (auto& ref: devices) {
        WebUsbDevice* dev = new WebUsbDevice(this, ref);
        known_devices_[dev->ref_->get_id()] = dev;
        on_found_.invoke(dev);
    }
}

void WebUsb::on_connect(const JsStub* args, size_t n_args) {
    if (n_args != 1) {
        F_LOG_W(logger_, "expected 1 args but got " << n_args << " args");
        return;
    }
    F_LOG_W(logger_, "device connected [TODO]");
}

void WebUsb::on_disconnect(const JsStub* args, size_t n_args) {
    if (n_args != 1) {
        F_LOG_W(logger_, "expected 1 args but got " << n_args << " args");
        return;
    }
    F_LOG_W(logger_, "device disconnected [TODO]");
}

RichStatus WebUsbDevice::get_info(uint8_t* bus, uint8_t* address, uint16_t* vendor_id, uint16_t* product_id) {
    if (bus || address) {
        return F_MAKE_ERR("Can't determine bus and address of device, WebUSB doesn't expose this information.");
    }
    if (vendor_id) {
        F_RET_IF_ERR(ref_->get_property("vendorId", vendor_id), "failed to get vendor ID");
    }
    if (product_id) {
        F_RET_IF_ERR(ref_->get_property("productId", product_id), "failed to get product ID");
    }
    return RichStatus::success();
}

RichStatus WebUsbDevice::with_active_config_desc(Callback<void, UsbConfigDesc*> callback) {
    JsObjectRef configuration;
    F_RET_IF_ERR(ref_->get_property("configuration", &configuration), "failed to load configuration");

    std::vector<JsObjectRef> interfaces;
    F_RET_IF_ERR(configuration->get_property("interfaces", &interfaces), "failed to load interfaces");
    std::vector<UsbInterfaceDesc> intf_descs{interfaces.size()};
    std::vector<std::vector<UsbAlternateDesc>> alt_descs{interfaces.size()};
    std::vector<std::vector<UsbEndpointDesc>> ep_descs{};

    for (size_t i = 0; i < interfaces.size(); ++i) {
        std::vector<JsObjectRef> alternates;
        F_RET_IF_ERR(interfaces[i]->get_property("alternates", &alternates), "failed to load alternates");
        alt_descs[i] = std::vector<UsbAlternateDesc>{alternates.size()};

        for (size_t j = 0; j < alternates.size(); ++j) {
            F_RET_IF_ERR(alternates[j]->get_property("interfaceClass", &alt_descs[i][j].interface_class), "failed to load alternate " << i << " " << j);
            F_RET_IF_ERR(alternates[j]->get_property("interfaceSubclass", &alt_descs[i][j].interface_subclass), "failed to load alternate " << i << " " << j);
            F_RET_IF_ERR(alternates[j]->get_property("interfaceProtocol", &alt_descs[i][j].interface_protocol), "failed to load alternate " << i << " " << j);

            std::vector<JsObjectRef> endpoints;
            F_RET_IF_ERR(alternates[j]->get_property("endpoints", &endpoints), "failed to load endpoints");
            ep_descs.push_back(std::vector<UsbEndpointDesc>{endpoints.size()});

            for (size_t k = 0; k < endpoints.size(); ++k) {
                std::string type;
                std::string direction;

                F_RET_IF_ERR(endpoints[k]->get_property("type", &type), "failed to load endpoint");
                F_RET_IF_ERR(endpoints[k]->get_property("direction", &direction), "failed to load endpoint");
                F_RET_IF_ERR(endpoints[k]->get_property("packetSize", &ep_descs.back()[k].max_packet_size), "failed to load endpoint");
                F_RET_IF_ERR(endpoints[k]->get_property("endpointNumber", &ep_descs.back()[k].number), "failed to load endpoint");

                if (type == "bulk") {
                    ep_descs.back()[k].type = UsbTransferType::kBulk;
                } else if (type == "interrupt") {
                    ep_descs.back()[k].type = UsbTransferType::kInterrupt;
                } else if (type == "isochronous") {
                    ep_descs.back()[k].type = UsbTransferType::kIsochronous;
                } else {
                    return F_MAKE_ERR("unknown transfer type " << type);
                }

                if (direction == "in") {
                    ep_descs.back()[k].number |= 0x80;
                }
            }

            alt_descs[i][j].endpoints = ep_descs.back().data();
            alt_descs[i][j].n_endpoints = ep_descs.back().size();
        }

        intf_descs[i] = UsbInterfaceDesc{
            alt_descs[i].size(),
            alt_descs[i].data()
        };
    }

    UsbConfigDesc config_desc{
        (uint8_t)intf_descs.size(),
        intf_descs.data()
    };

    callback.invoke(&config_desc);

    return RichStatus::success();
}

RichStatus WebUsbDevice::open(Callback<void, UsbDevice*> callback) {
    open_cb_ = callback;
    ref_->call_async("open", MEMBER_CB(this, on_open_finished), 0);
    return RichStatus::success();
}

RichStatus WebUsbDevice::claim_interface(uint8_t interface_num, Callback<void, UsbDevice*> callback) {
    claim_interface_cb_ = callback;
    ref_->call_async("claimInterface", MEMBER_CB(this, on_claim_interface_finished), 0, interface_num);
    return RichStatus::success();
}

RichStatus WebUsbDevice::bulk_in_transfer(uint8_t ep_num, bufptr_t buffer, Callback<void, RichStatus, unsigned char*> callback) {
    bulk_in_transfer_buf_ = buffer;
    bulk_in_transfer_cb_ = callback;
    F_LOG_D(webusb_->logger_, "bulk in " << buffer.size() << " bytes");
    ref_->call_async("transferIn", MEMBER_CB(this, on_bulk_in_transfer_finished), 1, ep_num & 0x7f, std::min(buffer.size(), 63UL));
    return RichStatus::success();
}

RichStatus WebUsbDevice::bulk_out_transfer(uint8_t ep_num, cbufptr_t buffer, Callback<void, RichStatus, const unsigned char*> callback) {
    bulk_out_transfer_buf_ = buffer;
    bulk_out_transfer_cb_ = callback;
    F_LOG_D(webusb_->logger_, "bulk out " << buffer.size() << " bytes");
    ref_->call_async("transferOut", MEMBER_CB(this, on_bulk_out_transfer_finished), 1, ep_num, buffer);
    return RichStatus::success();
}

void WebUsbDevice::on_open_finished(const JsStub& stub) {
    F_LOG_D(webusb_->logger_, "open finished");
    open_cb_.invoke_and_clear(this);
}

void WebUsbDevice::on_claim_interface_finished(const JsStub& stub) {
    F_LOG_D(webusb_->logger_, "claim_interface finished");
    claim_interface_cb_.invoke_and_clear(this);
}

void WebUsbDevice::on_bulk_in_transfer_finished(const JsStub& stub) {
    F_LOG_T(webusb_->logger_, "bulk_in_transfer finished");

    std::unordered_map<std::string, JsStub> result;
    if (F_LOG_IF_ERR(webusb_->logger_, from_js(stub, &result), "can't parse transfer result")) {
        return; // TODO: propagate error to callback
    }

    auto status_it = result.find("status");
    if (F_LOG_IF(webusb_->logger_, status_it == result.end(), "'status' not found")) {
        return;
    }

    std::string status;
    if (F_LOG_IF_ERR(webusb_->logger_, from_js(status_it->second, &status), "can't read status")) {
        return;
    }

    if (F_LOG_IF(webusb_->logger_, status != "ok", "transfer failed")) {
        return;
    }

    auto data_it = result.find("data");
    if (F_LOG_IF(webusb_->logger_, data_it == result.end(), "'data' not found")) {
        return;
    }

    cbufptr_t data;
    if (F_LOG_IF_ERR(webusb_->logger_, from_js(data_it->second, &data), "can't read data")) {
        return;
    }

    if (F_LOG_IF(webusb_->logger_, data.size() > bulk_in_transfer_buf_.size(), "more data than expected")) {
        return;
    }

    std::copy_n(data.begin(), data.size(), bulk_in_transfer_buf_.begin());
    auto end = bulk_in_transfer_buf_.begin() + data.size();

    bulk_in_transfer_buf_ = {};
    bulk_in_transfer_cb_.invoke_and_clear(RichStatus::success(), end);
}

void WebUsbDevice::on_bulk_out_transfer_finished(const JsStub& stub) {
    F_LOG_T(webusb_->logger_, "bulk_out_transfer finished");

    std::unordered_map<std::string, JsStub> result;
    if (F_LOG_IF_ERR(webusb_->logger_, from_js(stub, &result), "can't parse transfer result")) {
        return; // TODO: propagate error to callback
    }

    auto status_it = result.find("status");
    if (F_LOG_IF(webusb_->logger_, status_it == result.end(), "'status' not found")) {
        return;
    }

    std::string status;
    if (F_LOG_IF_ERR(webusb_->logger_, from_js(status_it->second, &status), "can't read status")) {
        return;
    }

    if (F_LOG_IF(webusb_->logger_, status != "ok", "transfer failed")) {
        return;
    }

    auto bytes_written_it = result.find("bytesWritten");
    if (F_LOG_IF(webusb_->logger_, bytes_written_it == result.end(), "'bytesWritten' not found")) {
        return;
    }

    size_t bytes_written;
    if (F_LOG_IF_ERR(webusb_->logger_, from_js(bytes_written_it->second, &bytes_written), "can't read bytes_written")) {
        return;
    }

    if (F_LOG_IF(webusb_->logger_, bytes_written > bulk_out_transfer_buf_.size(), "more bytes written than expected")) {
        return;
    }

    auto end = bulk_in_transfer_buf_.begin() + bytes_written;

    bulk_out_transfer_buf_ = {};
    bulk_out_transfer_cb_.invoke_and_clear(RichStatus::success(), end);
}

RichStatus WebusbBackend::init(EventLoop* event_loop, Logger logger) {
    logger_ = logger;

    webusb_ = new WebUsb(logger_);
    adapter_ = new UsbHostAdapter(logger_, webusb_);

    F_LOG_D(logger_, "init webusb backend");

    return RichStatus::success();
}

RichStatus WebusbBackend::deinit() {
    adapter_->stop();
    delete adapter_;
    delete webusb_;
    return RichStatus::success();
}

void WebusbBackend::start_channel_discovery(Domain* domain, const char* specs,
                                            size_t specs_len,
                                            ChannelDiscoveryContext** handle) {
    // TODO
    adapter_->start(domain, specs, specs_len);
}

RichStatus WebusbBackend::stop_channel_discovery(
    ChannelDiscoveryContext* handle) {
    // TODO
    return RichStatus::success();
}

#endif
