#ifndef __FIBRE_BLUETOOTH_DISCOVERER_HPP
#define __FIBRE_BLUETOOTH_DISCOVERER_HPP

#include <fibre/platform_support/linux_worker.hpp>
#include <fibre/platform_support/linux_timer.hpp>
#include <fibre/platform_support/dbus.hpp>
#include <fibre/uuid.hpp>
//#include "../../../platform_support/dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "../../../platform_support/dbus_interfaces/org.bluez.LEAdvertisingManager1.hpp"
#include "../../../platform_support/dbus_interfaces/org.bluez.GattManager1.hpp"

namespace fibre {

class LocalGattDescriptor;


// TODO: implement
class LocalGattCharacteristic {
public:
    LocalGattCharacteristic(Uuid uuid) : uuid_(uuid) {}
    Uuid get_uuid() { return uuid_; };

    int get_descriptors(LocalGattDescriptor** array, size_t* length);

    // TODO: make mutable

private:
    Uuid uuid_;
};

// Corresponds roughly to CBPeripheralManagerDelegate
// TODO: implement
class LocalGattService {
public:
    LocalGattService(Uuid uuid) : uuid_(uuid) {}
    Uuid get_uuid() { return uuid_; };

    int get_characteristics(LocalGattCharacteristic** array, size_t* length);

    // TODO: make mutable
    //CallbackList<LocalGattCharacteristic*> DidAddCharacteristic;
    //CallbackList<LocalGattCharacteristic*> WillRemoveCharacteristic;

private:
    Uuid uuid_;
};

class BluetoothPeripheralController {
public:
    struct Ad_t {
        bool is_connectable = true;
        bool include_tx_power = true;
        std::string local_name = "Hello World";
        Uuid service_uuid = "57155f13-33ec-456f-b9da-d2c876e2ecdc"; // TODO: allow more than 1 UUID
    };

    // TODO: find a good way to associate advertisements in "update/stop" calls
    virtual int start_advertising(Ad_t advertisement, void** token) = 0;
    virtual int update_advertisement(void*) = 0;
    virtual int stop_advertising(void*) = 0;

    virtual int register_service(LocalGattService* service) = 0;
    virtual int deregister_service(LocalGattService* service) = 0;
};

/**
 * @brief For internal use only.
 * Implements the Bluez DBus org.bluez.LEAdvertisement1 interface.
 */
class BluezAd {
public:
    BluezAd(BluetoothPeripheralController::Ad_t ad) {
        auto includes = std::vector<std::string>{};
        if (ad.include_tx_power)
            includes.push_back("tx-power");

        properties_ = {
            {"Type", ad.is_connectable ? std::string{"peripheral"} : std::string{"broadcast"}},
            {"ServiceUUIDs", std::vector<std::string>{ad.service_uuid.to_string()}},
            {"SolicitUUIDs", std::vector<std::string>{}},
            {"Includes", includes},
            {"LocalName", ad.local_name},
        };
    }

    void Release();
    dbus_variant Get(std::string interface, std::string name);
    std::unordered_map<std::string, fibre::dbus_variant> GetAll(std::string interface);
    void Set(std::string interface, std::string name, dbus_variant val);
    CallbackList<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged;

    std::string get_dbus_obj_path() { return "/ad_" + std::to_string(reinterpret_cast<intptr_t>(this)); }

private:
    std::unordered_map<std::string, fibre::dbus_variant> properties_;
};


/**
 * @brief For internal use only.
 * Implements the Bluez DBus org.bluez.GattService1 interface.
 */
class BluezService {
public:
    BluezService(LocalGattService* service) {
        properties_ = {
            {"UUID", std::string{"57155f13-33ec-456f-b9da-d2c876e2ecdc"}},
            {"Primary", true}
        };
    }

    dbus_variant Get(std::string interface, std::string name);
    std::unordered_map<std::string, fibre::dbus_variant> GetAll(std::string interface);
    void Set(std::string interface, std::string name, dbus_variant val);

    CallbackList<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged;

    std::string get_dbus_obj_name() { return "srv_" + std::to_string(reinterpret_cast<intptr_t>(this)); }

private:
    std::unordered_map<std::string, fibre::dbus_variant> properties_;
};


class BluezPeripheralController : public BluetoothPeripheralController {
public:
    int init(LinuxWorker* worker, DBusConnectionWrapper* dbus);
    int deinit();

    int start_advertising(Ad_t advertisement, void** token) final;
    int update_advertisement(void*) final;
    int stop_advertising(void*) final;

    int register_service(LocalGattService* service) final;
    int deregister_service(LocalGattService* service) final;

private:
    using adapter_t = DBusRemoteObject<org_bluez_LEAdvertisingManager1, org_bluez_GattManager1>;

    void handle_adapter_found(adapter_t* adapter);
    void handle_adapter_lost(adapter_t* adapter);
    void handle_ad_registered(org_bluez_LEAdvertisingManager1* mgr);
    void handle_ad_unregistered(org_bluez_LEAdvertisingManager1* mgr);
    void handle_app_registered(org_bluez_GattManager1* mgr);
    void handle_app_unregistered(org_bluez_GattManager1* mgr);

    LinuxWorker* worker_ = nullptr;
    DBusConnectionWrapper* dbus_ = nullptr;
    DBusLocalObjectManager dbus_obj_mgr_{};
    DBusRemoteObject<org_freedesktop_DBus_ObjectManager>* bluez_root_obj_ = nullptr;
    DBusDiscoverer<org_bluez_LEAdvertisingManager1, org_bluez_GattManager1> dbus_discoverer_{};

    std::mutex adapter_mutex_; // Any access to the adapter list or the ad list must be protected by this mutex

    std::vector<org_bluez_LEAdvertisingManager1*> adapters_;
    std::vector<BluezAd*> ads_;
    std::unordered_map<LocalGattService*, BluezService*> srvs_; // TODO: implement

    member_closure_t<decltype(&BluezPeripheralController::handle_adapter_found)> handle_adapter_found_obj_{&BluezPeripheralController::handle_adapter_found, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_adapter_lost)> handle_adapter_lost_obj_{&BluezPeripheralController::handle_adapter_lost, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_ad_registered)> handle_ad_registered_obj_{&BluezPeripheralController::handle_ad_registered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_ad_unregistered)> handle_ad_unregistered_obj_{&BluezPeripheralController::handle_ad_unregistered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_app_registered)> handle_app_registered_obj_{&BluezPeripheralController::handle_app_registered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_app_unregistered)> handle_app_unregistered_obj_{&BluezPeripheralController::handle_app_unregistered, this};
};

}

#endif // __FIBRE_BLUETOOTH_DISCOVERER_HPP