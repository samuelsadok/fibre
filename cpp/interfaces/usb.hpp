#ifndef __FIBRE_USB_HPP
#define __FIBRE_USB_HPP

#include <fibre/bufptr.hpp>
#include <fibre/callback.hpp>
#include <fibre/rich_status.hpp>

namespace fibre {

enum class UsbTransferType : uint8_t {
    kControl = 0,
    kIsochronous = 1,
    kInterrupt = 2,
    kBulk = 3,
    kBulkStream = 4,
};

struct UsbEndpointDesc {
    uint8_t number; // MSB indicates direction (0: OUT, 1: IN)
    uint16_t max_packet_size;
    UsbTransferType type;
};

struct UsbAlternateDesc {
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t n_endpoints;
    UsbEndpointDesc* endpoints;
};

struct UsbInterfaceDesc {
    size_t n_altsettings;
    UsbAlternateDesc* alternates;
};

struct UsbConfigDesc {
    uint8_t n_interfaces;
    UsbInterfaceDesc* interfaces;
};

struct UsbDevice {
    virtual RichStatus get_info(uint8_t* bus, uint8_t* address, uint16_t* vendor_id, uint16_t* product_id) = 0;
    virtual RichStatus with_active_config_desc(Callback<void, UsbConfigDesc*> callback) = 0;
    virtual RichStatus open(Callback<void, UsbDevice*> callback) = 0;
    virtual RichStatus claim_interface(uint8_t interface_num, Callback<void, UsbDevice*> callback) = 0;
    virtual RichStatus bulk_in_transfer(uint8_t ep_num, bufptr_t buffer, Callback<void, RichStatus, unsigned char*> callback) = 0;
    virtual RichStatus bulk_out_transfer(uint8_t ep_num, cbufptr_t buffer, Callback<void, RichStatus, const unsigned char*> callback) = 0;
};

class UsbHostController {
public:
    using on_found_device_t = Callback<void, UsbDevice*>;
    using on_lost_device_t = Callback<void, UsbDevice*>;

    /**
     * @brief Starts device enumeration.
     * 
     * All devices that are already connected when this function is called are
     * announced to on_found().
     * 
     * Subsequent connect and disconnect events are announced through on_found()
     * and on_lost().
     * 
     * Only one enumeration can be in progress at a time.
     */
    virtual RichStatus start(on_found_device_t on_found, on_lost_device_t on_lost) = 0;

    /**
     * @brief Stops device enumeration.
     * 
     * The callbacks that were given to on_found and on_lost are not invoked
     * anymore after calling stop().
     * 
     * on_lost() is not called for the devices that are connected during this
     * call.
     */
    virtual RichStatus stop() = 0;
};

}

#endif // __FIBRE_USB_HPP