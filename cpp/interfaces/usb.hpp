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
    uint8_t number; //!< MSB indicates direction (0: OUT, 1: IN)
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
    /**
     * @brief Returns basic information about the device.
     * 
     * Any parameter can be NULL and in this case no attempt is made to fetch
     * the information.
     * 
     * @param bus: Returns the bus number on which the device is connected.
     *        This value is not available on the WebUSB backend.
     * @param address: Returns the address which the device currently has. This
     *        can change after a replug or device reset event.
     *        This value is not available on the WebUSB backend.
     * @param vendor_id: Returns the vendor ID of the device.
     * @param product_id: Returns the product ID of the device.
     */
    virtual RichStatus get_info(uint8_t* bus, uint8_t* address, uint16_t* vendor_id, uint16_t* product_id) = 0;

    /**
     * @brief Loads the configuration descriptor of the currently active
     * configuration and passes it to the callback.
     * 
     * This function runs synchronously, meaning that callback is invoked
     * before this function returns. The descriptor is freed after the callback
     * returns.
     * 
     * If the active config descriptor cannot be loaded, an error is returned
     * and `callback` is not invoked.
     * 
     * @param callback: The callback that will be invoked with a pointer to the
     *        active config descriptor.
     */
    virtual RichStatus with_active_config_desc(Callback<void, UsbConfigDesc*> callback) = 0;

    /**
     * @brief Starts an async operation to open the device. This must be done
     * before `claim_interface` can be called.
     * 
     * @param callback: Will be called once the operation completes. The first
     *        argument indicates whether the operation was successful.
     */
    virtual RichStatus open(Callback<void, RichStatus, UsbDevice*> callback) = 0;

    /**
     * @brief Starts an async operation to claim the specified interface. This
     * must be done before a transfer can be issued on the associated endpoints.
     * 
     * @param callback: Will be called once the operation completes. The first
     *        argument indicates whether the operation was successful.
     */
    virtual RichStatus claim_interface(uint8_t interface_num, Callback<void, RichStatus, UsbDevice*> callback) = 0;

    /**
     * @brief Starts a bulk IN transfer (device => host).
     * 
     * @param ep_num: The endpoint number. The MSB is always 1 for IN endpoints.
     * @param buffer: The buffer into which data should be read.
     * @param callback: The callback that will be invoked when the operation
     *        completes. The first argument indicates whether the operation
     *        succeeded. The second argument indicates the (exclusive) end of
     *        the returned range.
     *        Possible reasons for failure include: stall condition, device
     *        unplugged, babble condition (device returned more data than
     *        requested).
     */
    virtual RichStatus bulk_in_transfer(uint8_t ep_num, bufptr_t buffer, Callback<void, RichStatus, unsigned char*> callback) = 0;

    /**
     * @brief Starts a bulk OUT transfer (host => device).
     * 
     * @param ep_num: The endpoint number. The MSB is always 0 for IN endpoints.
     * @param buffer: The buffer to be transferred.
     * @param callback: The callback that will be invoked when the operation
     *        completes. The first argument indicates whether the operation
     *        succeeded. The second argument indicates the (exclusive) end of
     *        the written range.
     *        Possible reasons for failure include: stall condition, device
     *        unplugged.
     */
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
     * 
     * When running in a browser with the WebUSB backend this only returns
     * devices for which the user has previously authorized the website (also if
     * the device is unplugged and replugged).
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

    /**
     * @brief Shows a platform-specific dialog where the user can select a USB
     * device to connect to.
     * 
     * This is only implemented on the WebUSB backend.
     * 
     * Once the user completes the dialog, the selected device(s), if any, will
     * be announced to the `on_found` callback passed to `start()`.
     * 
     * Returns an error if the dialog cannot be shown (e.g. because it's not
     * implemented on this platform).
     */
    virtual RichStatus request_device(std::optional<uint16_t> vendor_id, std::optional<uint16_t> product_id, std::optional<uint8_t> intf_class, std::optional<uint8_t> intf_subclass, std::optional<uint8_t> intf_protocol) = 0;
};

}

#endif // __FIBRE_USB_HPP