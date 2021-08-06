#ifndef __FIBRE_USB_DISCOVERER_HPP
#define __FIBRE_USB_DISCOVERER_HPP

#include <fibre/config.hpp>

#if FIBRE_ENABLE_WEBUSB_BACKEND

#include "dom_connector.hpp"
//#include "usb_host_adapter.hpp"
#include <fibre/channel_discoverer.hpp>
#include <fibre/logging.hpp>

namespace fibre {

struct UsbHostAdapter;
class WebUsb;

class WebusbBackend : public Backend {
public:
    RichStatus init(EventLoop* event_loop, Logger logger) final;
    RichStatus deinit() final;

    void start_channel_discovery(Domain* domain, const char* specs, size_t specs_len, ChannelDiscoveryContext** handle) final;
    RichStatus stop_channel_discovery(ChannelDiscoveryContext* handle) final;
    RichStatus show_device_dialog() final;

private:
    Logger logger_ = Logger::none();
    WebUsb* webusb_ = nullptr;
    UsbHostAdapter* adapter_ = nullptr;
};

}

#endif

#endif // __FIBRE_USB_DISCOVERER_HPP