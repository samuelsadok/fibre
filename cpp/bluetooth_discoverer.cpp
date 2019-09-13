
// Peripheral:
//  1. Instantiate org.bluez.GattService1 (contains the local characteristics)
//  2. Register service with org.bluez.GattManager1
//  3. Instantiate org.bluez.LEAdvertisement1
//  4. Register ad with org.bluez.LEAdvertisingManager1
//
// Central:
//  1. Instantiate org.bluez.GattProfile1 (contains a auto-connect UUID list)
//  2. Register profile with org.bluez.GattManager1

#include <fibre/bluetooth_discoverer.hpp>
#include "dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "dbus_interfaces/org.freedesktop.DBus.Properties.hpp"
#include "dbus_interfaces/org.bluez.LEAdvertisingManager1.hpp"
#include "dbus_interfaces/org.bluez.GattManager1.hpp"
#include "dbus_interfaces/org.bluez.LEAdvertisement1.hpp"
#include "dbus_interfaces/org.bluez.GattService1.hpp"

using namespace fibre;

DEFINE_LOG_TOPIC(BLUETOOTH);
USE_LOG_TOPIC(BLUETOOTH);

class Ad {
public:
    void Release() {
        FIBRE_LOG(D) << "Ad was released";
        // TODO: warn if the Ad was released unintentionally
    }

    dbus_variant Get(std::string interface, std::string name) {
        FIBRE_LOG(D) << "someone wants property " << name;
        return "";
    }
    std::unordered_map<std::string, fibre::dbus_variant> GetAll(std::string interface) {
        FIBRE_LOG(D) << "someone wants all properties";
        return {
            //{"Type", "peripheral"},
            //{"LocalName", "abc"},
            //{"ServiceUUIDs", std::vector<std::string>{"57155f13-33ec-456f-b9da-d2c876e2ecdc"}}
            //{"ServiceData", std::}

            {"Type", std::string{"broadcast"}},
            {"ServiceUUIDs", std::vector<std::string>{"57155f13-33ec-456f-b9da-d2c876e2ecdc"}},
            //{"ManufacturerData", std::unordered_map<uint16_t, fibre::dbus_variant>{}},
            {"SolicitUUIDs", std::vector<std::string>{}},
            {"Includes", std::vector<std::string>{"tx-power"/*, "local-name"*/}},
            //{"ServiceData", std::unordered_map<std::string, fibre::dbus_variant>{}},
            //{"IncludeTxPower", bool{true}},
            {"LocalName", std::string{"hello world"}},
            //{"Appearance", uint16_t{5}},
            //{"Duration", uint16_t{2}},
            //{"Timeout", uint16_t{10}}
        };
    }
    void Set(std::string interface, std::string name, dbus_variant val) {
        FIBRE_LOG(D) << "someone wants to set property " << name;
    }
    DBusSignal<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged;
};

class Srv {
public:

    dbus_variant Get(std::string interface, std::string name) {
        FIBRE_LOG(D) << "[GATTSERVICE] someone wants property " << name;
        return "";
    }
    std::unordered_map<std::string, fibre::dbus_variant> GetAll(std::string interface) {
        FIBRE_LOG(D) << "[GATTSERVICE] someone wants all properties";
        return {
            //{"Type", "peripheral"},
            //{"LocalName", "abc"},
            //{"ServiceUUIDs", std::vector<std::string>{"57155f13-33ec-456f-b9da-d2c876e2ecdc"}}
            //{"ServiceData", std::}

            {"Type", std::string{"broadcast"}},
            {"ServiceUUIDs", std::vector<std::string>{"57155f13-33ec-456f-b9da-d2c876e2ecdc"}},
            //{"ManufacturerData", std::unordered_map<uint16_t, fibre::dbus_variant>{}},
            {"SolicitUUIDs", std::vector<std::string>{}},
            {"Includes", std::vector<std::string>{"tx-power"/*, "local-name"*/}},
            //{"ServiceData", std::unordered_map<std::string, fibre::dbus_variant>{}},
            //{"IncludeTxPower", bool{true}},
            {"LocalName", std::string{"hello world"}},
            //{"Appearance", uint16_t{5}},
            //{"Duration", uint16_t{2}},
            //{"Timeout", uint16_t{10}}
        };
    }
    void Set(std::string interface, std::string name, dbus_variant val) {
        FIBRE_LOG(D) << "[GATTSERVICE] someone wants to set property " << name;
    }

    DBusSignal<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged;
};


static Ad ad;
static Srv srv;

int BluetoothCentralSideDiscoverer::init(Worker* worker, DBusConnectionWrapper* dbus) {
    worker_ = worker;
    dbus_ = dbus;

    bluez_root_obj_.~DBusRemoteObject();
    new (&bluez_root_obj_) decltype(bluez_root_obj_){{dbus_, "org.bluez", "/"}};
    return 0;
}

int BluetoothCentralSideDiscoverer::deinit() {
    worker_ = nullptr;
    dbus_ = nullptr;
    return 0;
}

int BluetoothCentralSideDiscoverer::start_ble_monitor() {
    if (!dbus_) {
        FIBRE_LOG(E) << "discoverer object not initialized";
        goto fail1;
    }
    if (dbus_->register_interfaces<org_bluez_LEAdvertisement1, org_freedesktop_DBus_Properties>(ad, &ad_obj_path) != 0) {
        FIBRE_LOG(E) << "failed to expose advertisement";
        goto fail2;
    }
    if (dbus_obj_mgr_.init(dbus_, "/test_obj") != 0) { // TODO: autogen name
        FIBRE_LOG(E) << "failed to init obj mgr";
        goto fail3;
    }
    srv_obj_path = "service0"; // todo: remove var
    if (dbus_obj_mgr_.add_interfaces<org_bluez_GattService1, org_freedesktop_DBus_Properties>(srv, srv_obj_path) != 0) {
        FIBRE_LOG(E) << "failed to expose service";
        goto fail4;
    }
    if (dbus_discoverer_.start(&bluez_root_obj_, &handle_adapter_found_obj_, &handle_adapter_lost_obj_) != 0) {
        goto fail5;
    }
    return 0;

fail5:
    dbus_obj_mgr_.remove_interfaces<org_bluez_GattService1, org_freedesktop_DBus_Properties>(srv_obj_path);
fail4:
    srv_obj_path = "";
    dbus_obj_mgr_.deinit();
fail3:
    dbus_->deregister_interfaces<org_bluez_LEAdvertisement1, org_freedesktop_DBus_Properties>(ad_obj_path);
fail2:
    ad_obj_path = "";
fail1:
    return -1;
}

int BluetoothCentralSideDiscoverer::stop_ble_monitor() {
    int result = 0;
    // TODO: unregister Ad
    if (dbus_discoverer_.stop() != 0) {
        FIBRE_LOG(E) << "failed to stop DBus discovery";
        result = -1;
    }
    if (dbus_obj_mgr_.remove_interfaces<org_bluez_LEAdvertisement1, org_freedesktop_DBus_Properties>(ad_obj_path) != 0) {
        FIBRE_LOG(E) << "failed to deregister DBus object";
        result = -1;
    }
    if (dbus_obj_mgr_.deinit() != 0) {
        FIBRE_LOG(E) << "failed to deinit DBus object manager";
        result = -1;
    }
    ad_obj_path = "";
    if (dbus_->deregister_interfaces<org_bluez_GattService1, org_freedesktop_DBus_Properties>(srv_obj_path) != 0) {
        FIBRE_LOG(E) << "failed to deregister DBus object";
        result = -1;
    }
    srv_obj_path = "";
    return result;
}

/**
 * @brief Starts creating channels that might help in finding the requested kind of objects.
 * 
 * If the function succeeds, an opaque context pointer is returned which must be
 * passed to stop_channel_discovery() to terminate this particular request.
 */
int BluetoothCentralSideDiscoverer::start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx) {
    // if there are already discovery requests in place, there's nothing to do
    if (!n_discovery_requests) {
        if (start_ble_monitor() != 0) {
            FIBRE_LOG(E) << "Failed to start bluetooth device discovery";
            return -1;
        }
    }
    n_discovery_requests++;
    return 0;
}

/**
 * @brief Stops an object discovery process that was started with start_channel_discovery()
 */
int BluetoothCentralSideDiscoverer::stop_channel_discovery(void* discovery_ctx) {
    int result = 0;
    if (n_discovery_requests == 1) {
        if (stop_ble_monitor() != 0) {
            FIBRE_LOG(E) << "Failed to stop bluetooth device discovery";
            result = -1;
        }
    }
    n_discovery_requests--;
    return result;
}

void BluetoothCentralSideDiscoverer::handle_adapter_found(adapter_t* adapter) {
    FIBRE_LOG(D) << "found BLE adapter " << adapter->base_;
    adapter->RegisterAdvertisement_async(ad_obj_path, {}, &handle_ad_registered_obj_); // TODO: not sure what the options list does
    adapter->RegisterApplication_async(dbus_obj_mgr_.get_path(), {}, &handle_srv_registered_obj_);
}

void BluetoothCentralSideDiscoverer::handle_adapter_lost(adapter_t* adapter) {
    FIBRE_LOG(D) << "lost BLE adapter " << adapter->base_;
    // TODO: cancel call to RegisterAdvertisement
}

void BluetoothCentralSideDiscoverer::handle_ad_registered(org_bluez_LEAdvertisingManager1* mgr) {
    FIBRE_LOG(D) << "ad registered";
}

void BluetoothCentralSideDiscoverer::handle_srv_registered(org_bluez_GattManager1* mgr) {
    FIBRE_LOG(D) << "service registered";
}

