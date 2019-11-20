#ifndef __FIBRE_BLUETOOTH_HPP
#define __FIBRE_BLUETOOTH_HPP

#include <fibre/uuid.hpp>
#include <fibre/stream.hpp>
#include <fibre/active_stream.hpp>

namespace fibre {


/**
 * @brief Represents a local GATT characteristic.
 * 
 * The StreamPusher emits packets on every write request and write command by the GATT client.
 * The StreamPuller pulls packets on every read request from the GATT client.
 * The StreamSink can be used to send notifications/indications to the GATT client.
 * To use the StreamSink functionality, either can_notify or can_indicate must
 * be true at the time when the characteristic is published.
 */
template<typename TBluetoothTypes>
struct LocalGattCharacteristic : public TBluetoothTypes::TLocalGattCharacteristicWriteAspect, public TBluetoothTypes::TLocalGattCharacteristicReadAspect, public TBluetoothTypes::TLocalGattCharacteristicNotifyAspect {
    LocalGattCharacteristic(Uuid uuid) : uuid(uuid) {}
    ~LocalGattCharacteristic() {}

    Uuid uuid;
    bool can_notify;
    bool can_indicate;

#if 0
    /**
     * @brief Data that is received from the Central (in a write request or
     * write command) is written to this sink.
     * 
     * If this field is NULL, the characteristic is marked unwritable.
     */
    StreamSink* sink;

    /**
     * @brief Data that is requested from the Central (in read request) is
     * written to this sink.
     * 
     * If this field is NULL, the characteristic is marked unreadable.
     * 
     * TODO: if using notify semantics, the bluetooth driver should instead
     * provide a StreamSink where we can push data at will.
     */
    StreamSource* source;
#endif
};

// Corresponds roughly to CBPeripheralManagerDelegate
template<typename TBluetoothTypes>
class LocalGattService {
public:
    LocalGattService(Uuid uuid,
                     typename TBluetoothTypes::TLocalGattCharacteristic* characteristics,
                     size_t n_characteristics) : uuid_(uuid), characteristics_(characteristics), n_characteristics_(n_characteristics) {}
    Uuid get_uuid() { return uuid_; };

    int get_characteristics(typename TBluetoothTypes::TLocalGattCharacteristic** array, size_t* length) {
        if (array)
            *array = characteristics_;
        if (length)
            *length = n_characteristics_;
        return 0;
    }

    // TODO: make mutable
    //CallbackList<LocalGattCharacteristic*> DidAddCharacteristic;
    //CallbackList<LocalGattCharacteristic*> WillRemoveCharacteristic;

private:
    Uuid uuid_;
    typename TBluetoothTypes::TLocalGattCharacteristic* characteristics_;
    size_t n_characteristics_;
};

template<typename TBluetoothTypes>
class BluetoothPeripheralController {
public:
    struct Ad_t {
        /**
         * @brief If true, the advertisement indicates that the device is
         * connectable.
         * 
         * Peripherals and Centrals should set this to true.
         * Broadcasters (Beacons) should set this to false.
         */
        bool is_connectable; /* = true;*/

        /**
         * @brief If true, the TX power is included in the advertisement.
         * How to set the TX power depends on the implementation.
         */
        bool include_tx_power; /* = true;*/

        /**
         * @brief If true, a 16-bit device appearance code is included in the
         * advertisement. How to define this code depends on the implementation.
         * 
         * The appearance codes are defined here:
         * http://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.gap.appearance.xml
         */
        bool include_appearance; /* = true;*/

        Uuid service_uuid; /* = "57155f13-33ec-456f-b9da-d2c876e2ecdc"; // TODO: allow more than 1 UUID*/

        /**
         * @brief The local name of the device. Set to "" to omit.
         * 
         * TODO: this may depend on GAP configuration.
         */
        std::string local_name; /* = "Hello World";*/

        /** @brief Pointer to the manufacturer data. Set to NULL to omit. */
        uint8_t* manufacturer_data;

        /** @brief Length of the manufacturer data */
        size_t manufacturer_data_length;

        /** @brief 16-bit manufacturer ID. Only used if manufacturer data is supplied. */
        uint16_t manufacturer_id;
        
        /** @brief Additional data to send in response to a scan. */
        Ad_t* scan_response_data;
    };

    // TODO: find a good way to associate advertisements in "update/stop" calls
    virtual int start_advertising(Ad_t advertisement, uintptr_t* handle) = 0;
    virtual int update_advertisement(uintptr_t handle) = 0;
    virtual int stop_advertising(uintptr_t handle) = 0;

    virtual int register_service(typename TBluetoothTypes::TLocalGattService* service) = 0;
    virtual int deregister_service(typename TBluetoothTypes::TLocalGattService* service) = 0;
};

}

#endif // __FIBRE_BLUETOOTH_HPP