#ifndef __FIBRE_BLUETOOTH_DISCOVERER_HPP
#define __FIBRE_BLUETOOTH_DISCOVERER_HPP

#include <fibre/platform_support/linux_worker.hpp>
#include <fibre/platform_support/linux_timer.hpp>
#include <fibre/platform_support/dbus.hpp>
#include <fibre/bluetooth.hpp>
#include <fibre/uuid.hpp>
//#include "../../../platform_support/dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "../../../platform_support/dbus_interfaces/org.bluez.LEAdvertisingManager1.hpp"
#include "../../../platform_support/dbus_interfaces/org.bluez.GattManager1.hpp"

namespace fibre {

class BluezLocalGattCharacteristic;
class BluezLocalGattService;
class BluezPeripheralController;

struct BluezBluetoothTypes {
    struct dummy {};
    using TWorker = int; // placeholder
    using TLocalGattCharacteristic = BluezLocalGattCharacteristic;
    using TLocalGattService = BluezLocalGattService;
    using TPeripheralController = BluezPeripheralController;
    using TLocalGattCharacteristicReadAspect = StreamPuller;
    using TLocalGattCharacteristicWriteAspect = StreamPusherIntBuffer;
    using TLocalGattCharacteristicNotifyAspect = dummy; // placeholder
};

/**
 * @brief For internal use only.
 * Implements the Bluez DBus org.bluez.LEAdvertisement1 interface.
 */
class BluezAd {
public:
    BluezAd(BluetoothPeripheralController<BluezBluetoothTypes>::Ad_t ad) {
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
class BluezLocalGattService : public LocalGattService<BluezBluetoothTypes> {
public:
    BluezLocalGattService(Uuid uuid, BluezLocalGattCharacteristic* characteristics, size_t n_characteristics)
        : LocalGattService(uuid, characteristics, n_characteristics) {
        properties_ = {
            {"UUID", uuid.to_string()},
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

    friend class BluezPeripheralController;
};

class BluezLocalGattCharacteristic : public LocalGattCharacteristic<BluezBluetoothTypes> {
public:
    BluezLocalGattCharacteristic(Uuid uuid)
        : LocalGattCharacteristic(uuid) {
        properties_ = {
            {"UUID", uuid.to_string()}
        };
    }

    dbus_variant Get(std::string interface, std::string name);
    std::unordered_map<std::string, fibre::dbus_variant> GetAll(std::string interface);
    void Set(std::string interface, std::string name, dbus_variant val);

    CallbackList<std::string, std::unordered_map<std::string, fibre::dbus_variant>, std::vector<std::string>> PropertiesChanged;

    std::vector<uint8_t> ReadValue(std::unordered_map<std::string, dbus_variant> options);
    void WriteValue(std::vector<uint8_t> value, std::unordered_map<std::string, dbus_variant> options);
    void StartNotify(void);
    void StopNotify(void);

    std::string get_dbus_obj_name() { return (parent_ ? parent_->get_dbus_obj_name() : "???") + "/char_" + std::to_string(reinterpret_cast<intptr_t>(this)); }

private:
    BluezLocalGattService* parent_ = nullptr;
    std::unordered_map<std::string, fibre::dbus_variant> properties_;

    friend class BluezPeripheralController;
};


class BluezPeripheralController : public BluetoothPeripheralController<BluezBluetoothTypes> {
public:
    int init(LinuxWorker* worker, DBusConnectionWrapper* dbus);
    int deinit();

    int start_advertising(Ad_t advertisement, uintptr_t* handle) final;
    int update_advertisement(uintptr_t handle) final;
    int stop_advertising(uintptr_t handle) final;

    int register_service(BluezLocalGattService* service) final;
    int deregister_service(BluezLocalGattService* service) final;

private:
    using adapter_t = DBusRemoteObject<org_bluez_LEAdvertisingManager1, org_bluez_GattManager1>;

    void handle_adapter_found(adapter_t* adapter);
    void handle_adapter_lost(adapter_t* adapter);
    void handle_adapter_search_stopped();
    void handle_ad_registered(org_bluez_LEAdvertisingManager1* mgr);
    void handle_ad_register_failed(org_bluez_LEAdvertisingManager1* mgr);
    void handle_ad_unregistered(org_bluez_LEAdvertisingManager1* mgr);
    void handle_ad_unregister_failed(org_bluez_LEAdvertisingManager1* mgr);
    void handle_app_registered(org_bluez_GattManager1* mgr);
    void handle_app_register_failed(org_bluez_GattManager1* mgr);
    void handle_app_unregistered(org_bluez_GattManager1* mgr);
    void handle_app_unregister_failed(org_bluez_GattManager1* mgr);

    LinuxWorker* worker_ = nullptr;
    DBusConnectionWrapper* dbus_ = nullptr;
    DBusLocalObjectManager dbus_obj_mgr_{};
    DBusRemoteObject<org_freedesktop_DBus_ObjectManager>* bluez_root_obj_ = nullptr;
    DBusDiscoverer<org_bluez_LEAdvertisingManager1, org_bluez_GattManager1> dbus_discoverer_{};

    std::mutex adapter_mutex_; // Any access to the adapter list or the ad list must be protected by this mutex

    std::vector<adapter_t*> adapters_;
    std::vector<BluezAd*> ads_;
    size_t num_services_ = 0;

    member_closure_t<decltype(&BluezPeripheralController::handle_adapter_found)> handle_adapter_found_obj_{&BluezPeripheralController::handle_adapter_found, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_adapter_lost)> handle_adapter_lost_obj_{&BluezPeripheralController::handle_adapter_lost, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_adapter_search_stopped)> handle_adapter_search_stopped_obj_{&BluezPeripheralController::handle_adapter_search_stopped, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_ad_registered)> handle_ad_registered_obj_{&BluezPeripheralController::handle_ad_registered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_ad_register_failed)> handle_ad_register_failed_obj_{&BluezPeripheralController::handle_ad_register_failed, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_ad_unregistered)> handle_ad_unregistered_obj_{&BluezPeripheralController::handle_ad_unregistered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_ad_unregister_failed)> handle_ad_unregister_failed_obj_{&BluezPeripheralController::handle_ad_unregister_failed, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_app_registered)> handle_app_registered_obj_{&BluezPeripheralController::handle_app_registered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_app_register_failed)> handle_app_register_failed_obj_{&BluezPeripheralController::handle_app_register_failed, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_app_unregistered)> handle_app_unregistered_obj_{&BluezPeripheralController::handle_app_unregistered, this};
    member_closure_t<decltype(&BluezPeripheralController::handle_app_unregister_failed)> handle_app_unregister_failed_obj_{&BluezPeripheralController::handle_app_unregister_failed, this};
};

}

#endif // __FIBRE_BLUETOOTH_DISCOVERER_HPP