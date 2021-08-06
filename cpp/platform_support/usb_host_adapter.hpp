#ifndef __FIBRE_USB_ADAPTER_HPP
#define __FIBRE_USB_ADAPTER_HPP

#include "../interfaces/usb.hpp"
#include <fibre/logging.hpp>
#include <unordered_map>

namespace fibre {

class Domain;
struct OpenDevice;

struct UsbHostAdapter {
    UsbHostAdapter(Logger logger, UsbHostController* usb) : logger_(logger), usb_(usb) {}

    void start(Domain* domain, const char* specs, size_t specs_len);
    void stop();
    RichStatus show_device_dialog();

private:
    struct InterfaceSpecs {
        int bus = -1; // -1 to ignore
        int address = -1; // -1 to ignore
        int vendor_id = -1; // -1 to ignore
        int product_id = -1; // -1 to ignore
        int interface_class = -1; // -1 to ignore
        int interface_subclass = -1; // -1 to ignore
        int interface_protocol = -1; // -1 to ignore
    };

    RichStatus consider(UsbDevice* device, InterfaceSpecs* specs);
    void on_found_device(UsbDevice* device);
    void on_lost_device(UsbDevice* device);
    void on_opened_device(RichStatus status, UsbDevice* device);
    void on_claimed_interface(RichStatus status, UsbDevice* device);

    Logger logger_;
    Domain* domain_;
    UsbHostController* usb_;
    InterfaceSpecs specs_;
    std::unordered_map<UsbDevice*, OpenDevice*> open_devices_;
};

}

#endif // __FIBRE_USB_ADAPTER_HPP
