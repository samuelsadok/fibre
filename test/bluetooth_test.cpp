
// note: on my machine the bluetooth card is on usb1/1-8, it can thus be removed
// and added back via
//
// echo "1-8" | sudo tee /sys/bus/usb/drivers/usb/unbind
// echo "1-8" | sudo tee /sys/bus/usb/drivers/usb/bind

#include <fibre/platform_support/linux_worker.hpp>
#include <fibre/platform_support/bluez.hpp>

#include "test_utils.hpp"

using namespace fibre;

using TBluetoothTypes = BluezBluetoothTypes;


constexpr const size_t kMaxEchoLength = 64;

class EchoCharacteristic : public StreamSinkIntBuffer, public StreamSourceIntBuffer {
public:
    StreamStatus get_buffer(bufptr_t* buf) final {
        if (buf) {
            buf->ptr = buffer_;
            buf->length = std::min(sizeof(buffer_), buf->length);
        }
        return kStreamOk;
    }

    StreamStatus commit(size_t length) final {
        length_ = std::min(sizeof(buffer_), length);

        for (size_t i = 0; i < length_; ++i) {
            buffer_[i] += 2;
        }

        return kStreamOk;
    }

    StreamStatus get_buffer(cbufptr_t* buf) final {
        if (buf) {
            buf->ptr = buffer_;
            buf->length = std::min(length_, buf->length);
        }
        return kStreamOk;
    }

    StreamStatus consume(size_t length) final {
        // Nothing to do. Data will always remain available.
        return kStreamOk;
    }

private:
    uint8_t buffer_[kMaxEchoLength] = {0x1, 0x2, 0x3};
    size_t length_ = 3;
};


int main(int argc, const char** argv) {
    TestContext context;

    LinuxWorker worker;
    TEST_ZERO(worker.init());

    DBusConnectionWrapper dbus_connection;
    TEST_ZERO(dbus_connection.init(&worker, true));

    BluezPeripheralController peripheral;
    TEST_ZERO(peripheral.init(&worker, &dbus_connection));

    EchoCharacteristic ch0;
    EchoCharacteristic ch1;

    BluetoothPeripheralController<TBluetoothTypes>::Ad_t my_ad = {
        .is_connectable = true,
        .include_tx_power = true,
        .service_uuid = "57155f13-33ec-456f-b9da-d2c876e2ecdc",
        .local_name = "Hello World",
    };

    TBluetoothTypes::TLocalGattCharacteristic my_characteristics[] = {
        {"57150001-33ec-456f-b9da-d2c876e2ecdc"},
        {"57150002-33ec-456f-b9da-d2c876e2ecdc"}
    };
    
    int terminated_called = 0;
    auto terminated_callback = make_lambda_closure([&terminated_called](StreamStatus){
        terminated_called++;
    });

    connect_streams((TBluetoothTypes::TLocalGattCharacteristicReadAspect*)&my_characteristics[0], &ch0, &terminated_callback);
    connect_streams(&ch0, (TBluetoothTypes::TLocalGattCharacteristicWriteAspect*)&my_characteristics[0], &terminated_callback);

    connect_streams((TBluetoothTypes::TLocalGattCharacteristicReadAspect*)&my_characteristics[1], &ch1, &terminated_callback);
    connect_streams(&ch1, (TBluetoothTypes::TLocalGattCharacteristicWriteAspect*)&my_characteristics[1], &terminated_callback);

    TBluetoothTypes::TLocalGattService my_service{"57155f13-33ec-456f-b9da-d2c876e2ecdc", my_characteristics, sizeof(my_characteristics) / sizeof(my_characteristics[0])};

    printf("press [ENTER] to register service and start advertising\n");
    getchar(); // TODO: make stdin unbuffered

    uintptr_t token;
    TEST_ZERO(peripheral.register_service(&my_service));
    TEST_ZERO(peripheral.start_advertising(my_ad, &token));
    
    printf("press [ENTER] to stop advertising and deregister service\n");
    getchar(); // TODO: make stdin unbuffered
    printf("done...\n");

    TEST_ZERO(peripheral.stop_advertising(token));
    TEST_ZERO(peripheral.deregister_service(&my_service));
    TEST_ZERO(peripheral.deinit());

    /*BluezBluetoothCentral central;

    central.start_scan();
    central.connect(peripheral);
    central.stop_scan();

    LocalPeripheral peripheral;
    peripheral*/

    TEST_ZERO(dbus_connection.deinit());
    TEST_ZERO(worker.deinit());

    return context.summarize();
}
