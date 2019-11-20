
// Peripheral:
//  1. Instantiate org.bluez.GattService1 (contains the local characteristics)
//  2. Register service with org.bluez.GattManager1
//  3. Instantiate org.bluez.LEAdvertisement1
//  4. Register ad with org.bluez.LEAdvertisingManager1
//
// Central:
//  1. Instantiate org.bluez.GattProfile1 (contains a auto-connect UUID list)
//  2. Register profile with org.bluez.GattManager1
//
// Note: DBus interface specification can be found here:
// https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/
// and some XMLs: https://github.com/labapart/gattlib/tree/51c6cbeee2c7af15347d40389dd9a05ac829f674/dbus/dbus-bluez-v5.48

#include <fibre/platform_support/bluez.hpp>
#include "../platform_support/dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "../platform_support/dbus_interfaces/org.freedesktop.DBus.Properties.hpp"
#include "../platform_support/dbus_interfaces/org.bluez.LEAdvertisingManager1.hpp"
#include "../platform_support/dbus_interfaces/org.bluez.GattManager1.hpp"
#include "../platform_support/dbus_interfaces/org.bluez.LEAdvertisement1.hpp"
#include "../platform_support/dbus_interfaces/org.bluez.GattService1.hpp"
#include "../platform_support/dbus_interfaces/org.bluez.GattCharacteristic1.hpp"

using namespace fibre;

DEFINE_LOG_TOPIC(BLUETOOTH);
USE_LOG_TOPIC(BLUETOOTH);

/* BluezAd implementation ----------------------------------------------------*/

void BluezAd::Release() {
    FIBRE_LOG(D) << "Ad was released";
    // TODO: warn if the Ad was released unintentionally
}

dbus_variant BluezAd::Get(std::string interface, std::string name) {
    FIBRE_LOG(D) << "[AD] someone wants property " << name;
    auto it = properties_.find(name);
    if (it == properties_.end()) {
        FIBRE_LOG(E) << "invalid property fetched";
        return ""; // TODO return error
    }
    return it->second;
}

std::unordered_map<std::string, fibre::dbus_variant> BluezAd::GetAll(std::string interface) {
    FIBRE_LOG(D) << "[AD] someone wants all properties";
    return properties_;
}
void BluezAd::Set(std::string interface, std::string name, dbus_variant val) {
    FIBRE_LOG(E) << "[AD] someone wants to set property " << name;
    // TODO: return error
}

/* BluezLocalGattCharacteristic implementation -------------------------------*/

dbus_variant BluezLocalGattCharacteristic::Get(std::string interface, std::string name) {
    FIBRE_LOG(D) << "[GATTCHAR] someone wants property " << name;
    auto it = properties_.find(name);
    if (it == properties_.end()) {
        FIBRE_LOG(E) << "invalid property fetched";
        return ""; // TODO return error
    }
    return it->second;
}

std::unordered_map<std::string, fibre::dbus_variant> BluezLocalGattCharacteristic::GetAll(std::string interface) {
    FIBRE_LOG(D) << "[GATTCHAR] someone wants all properties";
    return properties_;
}

void BluezLocalGattCharacteristic::Set(std::string interface, std::string name, dbus_variant val) {
    FIBRE_LOG(E) << "[GATTCHAR] someone wants to set property " << name;
    // TODO: return error
}

std::vector<uint8_t> BluezLocalGattCharacteristic::ReadValue(std::unordered_map<std::string, dbus_variant> options) {
    FIBRE_LOG(W) << "ReadValue called";
    return {1, 2, 3};
}

void BluezLocalGattCharacteristic::WriteValue(std::vector<uint8_t> value, std::unordered_map<std::string, dbus_variant> options) {
    FIBRE_LOG(W) << "WriteValue called with " << value;
    return;
}

void BluezLocalGattCharacteristic::StartNotify(void) {
    FIBRE_LOG(W) << "StartNotify() called but not implemented";
}

void BluezLocalGattCharacteristic::StopNotify(void) {
    FIBRE_LOG(W) << "StopNotify() called but not implemented";
}


/* BluezLocalGattService implementation --------------------------------------*/

dbus_variant BluezLocalGattService::Get(std::string interface, std::string name) {
    FIBRE_LOG(D) << "[GATTSERVICE] someone wants property " << name;
    auto it = properties_.find(name);
    if (it == properties_.end()) {
        FIBRE_LOG(E) << "invalid property fetched";
        return ""; // TODO return error
    }
    return it->second;
}

std::unordered_map<std::string, fibre::dbus_variant> BluezLocalGattService::GetAll(std::string interface) {
    FIBRE_LOG(D) << "[GATTSERVICE] someone wants all properties";
    return properties_;
}

void BluezLocalGattService::Set(std::string interface, std::string name, dbus_variant val) {
    FIBRE_LOG(E) << "[GATTSERVICE] someone wants to set property " << name;
    // TODO: return error
}


/* BluezPeripheralController implementation ----------------------------------*/

int BluezPeripheralController::init(LinuxWorker* worker, DBusConnectionWrapper* dbus) {
    worker_ = worker;
    dbus_ = dbus;
    bluez_root_obj_ = new DBusRemoteObject<org_freedesktop_DBus_ObjectManager>{{dbus_, "org.bluez", "/"}};

    if (dbus_obj_mgr_.init(dbus_, "/test_obj") != 0) { // TODO: autogen name
        FIBRE_LOG(E) << "failed to init obj mgr";
        goto fail2;
    }

    if (dbus_discoverer_.start(bluez_root_obj_, &handle_adapter_found_obj_, &handle_adapter_lost_obj_, &handle_adapter_search_stopped_obj_) != 0) {
        goto fail1;
    }

    return 0;

fail2:
    dbus_obj_mgr_.deinit();
fail1:
    delete bluez_root_obj_;
    bluez_root_obj_ = nullptr;
    worker_ = nullptr;
    dbus_ = nullptr;
    return -1;
}

int BluezPeripheralController::deinit() {
    int result = 0;

    if (ads_.size() != 0) {
        FIBRE_LOG(E) << "not all ads were stopped";
        result = -1;
    }

    if (num_services_ != 0) {
        FIBRE_LOG(E) << "not all services were deregistered";
        result = -1;
    }

    if (dbus_discoverer_.stop() != 0) {
        FIBRE_LOG(E) << "failed to stop DBus discovery";
        result = -1;
    }

    if (dbus_obj_mgr_.deinit() != 0) {
        FIBRE_LOG(E) << "failed to deinit DBus object manager";
        result = -1;
    }

    delete bluez_root_obj_;
    bluez_root_obj_ = nullptr;
    worker_ = nullptr;
    dbus_ = nullptr;
    return 0;
}

int BluezPeripheralController::start_advertising(Ad_t advertisement, uintptr_t* token) {
    BluezAd* internal_ad;

    if (!dbus_) {
        FIBRE_LOG(E) << "bluez not initialized";
        goto fail1;
    }

    internal_ad = new BluezAd(advertisement);

    if (dbus_->register_interfaces<org_bluez_LEAdvertisement1, org_freedesktop_DBus_Properties>(*internal_ad, internal_ad->get_dbus_obj_path()) != 0) {
        FIBRE_LOG(E) << "failed to expose advertisement";
        goto fail2;
    }

    {
        std::unique_lock<std::mutex> lock{adapter_mutex_};
        for (auto& adapter : adapters_) {
            FIBRE_LOG(D) << "register ad on " << *adapter;
            adapter->RegisterAdvertisement_async(internal_ad->get_dbus_obj_path(), {}, &handle_ad_registered_obj_, &handle_ad_register_failed_obj_);
        }
        ads_.push_back(internal_ad);
    }

    if (token)
        *token = reinterpret_cast<uintptr_t>(internal_ad);
    
    return 0;

fail2:
    delete internal_ad;
fail1:
    return -1;
}

int BluezPeripheralController::update_advertisement(uintptr_t token) {
    FIBRE_LOG(E) << "not implemented";
    return -1;
}

int BluezPeripheralController::stop_advertising(uintptr_t token) {
    int result = 0;
    BluezAd* internal_ad = reinterpret_cast<BluezAd*>(token);

    {
        std::unique_lock<std::mutex> lock{adapter_mutex_};
        auto it = std::find_if(ads_.begin(), ads_.end(), [internal_ad](BluezAd* const & ad){ return internal_ad == ad; });
        if (it == ads_.end()) {
            FIBRE_LOG(E) << "not registered";
            return -1;
        }

        ads_.erase(it);
        for (auto& adapter : adapters_) {
            FIBRE_LOG(D) << "unregister ad on " << *adapter;
            adapter->UnregisterAdvertisement_async(internal_ad->get_dbus_obj_path(), &handle_ad_unregistered_obj_, &handle_ad_unregister_failed_obj_);
        }
    }
    
    // TODO: wait for unregistering to complete
    if (dbus_->deregister_interfaces<org_bluez_LEAdvertisement1, org_freedesktop_DBus_Properties>(internal_ad->get_dbus_obj_path()) != 0) {
        FIBRE_LOG(E) << "failed to deregister DBus object";
        result = -1;
    }

    delete internal_ad;
    return result;
}

int BluezPeripheralController::register_service(BluezLocalGattService* service) {
    //auto internal_service = new BluezLocalGattService(service);

    size_t i = 0;

    BluezLocalGattCharacteristic* characteristics;
    size_t n_characteristics;

    if (service->get_characteristics(&characteristics, &n_characteristics)) {
        goto fail1;
    }

    // Init and add all characteristics to DBus object manager
    for (i = 0; i < n_characteristics; ++i) {
        characteristics[i].parent_ = service;
        characteristics[i].properties_["Service"] = DBusObjectPath("/test_obj/" + service->get_dbus_obj_name());

        std::vector<std::string> flags;
        flags.push_back("read");
        if (characteristics[i].source_) {
            flags.push_back("read");
            // TODO: investigate characteristic broadcast feature
        }
        if (characteristics[i].sink_) {
            flags.push_back("write");
            flags.push_back("write-without-response");
        }
        // TODO: support indicate/notify

        characteristics[i].properties_["Flags"] = flags;

        if (dbus_obj_mgr_.add_interfaces<org_bluez_GattCharacteristic1, org_freedesktop_DBus_Properties>(characteristics[i], characteristics[i].get_dbus_obj_name()) != 0) {
            FIBRE_LOG(E) << "failed to expose characteristic";
            goto fail2;
        }
    }

    // Add service to DBus object manager
    if (dbus_obj_mgr_.add_interfaces<org_bluez_GattService1, org_freedesktop_DBus_Properties>(*service, service->get_dbus_obj_name()) != 0) {
        FIBRE_LOG(E) << "failed to expose service";
        goto fail2;
    }

    // Register DBus object manager with Bluez adapters
    if (num_services_ == 0) {
        for (auto& adapter : adapters_) {
            FIBRE_LOG(D) << "register application on " << adapter;
            adapter->RegisterApplication_async(dbus_obj_mgr_.get_path(), {}, &handle_app_registered_obj_, &handle_app_register_failed_obj_);
        }
    }

    num_services_++;
    return 0;

fail2:
    // Remove added characteristics
    for (; i > 0; --i) {
        dbus_obj_mgr_.remove_interfaces<org_bluez_GattCharacteristic1, org_freedesktop_DBus_Properties>(characteristics[i - 1].get_dbus_obj_name());
    }
fail1:
    return -1;
}

int BluezPeripheralController::deregister_service(BluezLocalGattService* service) {
    int result = 0;
    if (dbus_obj_mgr_.remove_interfaces<org_bluez_GattService1, org_freedesktop_DBus_Properties>(service->get_dbus_obj_name()) != 0) {
        FIBRE_LOG(E) << "failed to remove service";
        result = -1;
    }

    BluezLocalGattCharacteristic* characteristics;
    size_t n_characteristics;
    if (service->get_characteristics(&characteristics, &n_characteristics)) {
        FIBRE_LOG(E) << "failed remove characteristics";
        result = -1;
    } else {
        for (size_t i = 0; i < n_characteristics; ++i) {
            if (dbus_obj_mgr_.remove_interfaces<org_bluez_GattCharacteristic1, org_freedesktop_DBus_Properties>(characteristics[i].get_dbus_obj_name()) != 0) {
                FIBRE_LOG(E) << "failed to remove characteristic";
                result = -1;
            }
        }
    }

    if (num_services_ == 1) {
        for (auto& adapter : adapters_) {
            //FIBRE_LOG(D) << "register application on " << *adapter->base_;
            adapter->UnregisterApplication_async(dbus_obj_mgr_.get_path(), &handle_app_unregistered_obj_, &handle_app_unregister_failed_obj_);
        }
    }

    num_services_--;

    return result;
}

void BluezPeripheralController::handle_adapter_found(adapter_t* adapter) {
    std::unique_lock<std::mutex> lock{adapter_mutex_};

    FIBRE_LOG(D) << "found BLE adapter " << adapter->base_;

    // Bluez doesn't like applications that don't expose anything.
    // This call would fail with "No object received".
    // Therefore we don't register the application until we have at least one service.
    if (num_services_ > 0) {
        adapter->RegisterApplication_async(dbus_obj_mgr_.get_path(), {}, &handle_app_registered_obj_, &handle_app_register_failed_obj_);
    }

    // Register existing ads with this adapter
    for (auto& ad : ads_) {
        adapter->RegisterAdvertisement_async(ad->get_dbus_obj_path(), {}, &handle_ad_registered_obj_, &handle_ad_register_failed_obj_);
    }

    adapters_.push_back(adapter);
}

void BluezPeripheralController::handle_adapter_lost(adapter_t* adapter) {
    std::unique_lock<std::mutex> lock{adapter_mutex_};

    FIBRE_LOG(D) << "lost BLE adapter " << adapter->base_;

    // TODO: cancel call to RegisterApplication
    // TODO: cancel calls to RegisterAdvertisement

    adapters_.erase(std::remove(adapters_.begin(), adapters_.end(), adapter), adapters_.end());

    for (auto& ad : ads_) {
        adapter->UnregisterAdvertisement_async(ad->get_dbus_obj_path(), &handle_ad_unregistered_obj_, &handle_ad_unregister_failed_obj_); // TODO: not sure what the options list does
    }

    if (num_services_ > 0) {
        adapter->UnregisterApplication_async(dbus_obj_mgr_.get_path(), &handle_app_unregistered_obj_, &handle_app_unregister_failed_obj_);
    }
}

void BluezPeripheralController::handle_adapter_search_stopped() {
    FIBRE_LOG(E) << "failed to search for adapters";
}

void BluezPeripheralController::handle_ad_registered(org_bluez_LEAdvertisingManager1* mgr) {
    FIBRE_LOG(D) << "ad registered";
}

void BluezPeripheralController::handle_ad_register_failed(org_bluez_LEAdvertisingManager1* mgr) {
    FIBRE_LOG(E) << "failed to register ad";
}

void BluezPeripheralController::handle_ad_unregistered(org_bluez_LEAdvertisingManager1* mgr) {
    FIBRE_LOG(D) << "ad unregistered";
}

void BluezPeripheralController::handle_ad_unregister_failed(org_bluez_LEAdvertisingManager1* mgr) {
    FIBRE_LOG(E) << "failed to unregister ad";
}

void BluezPeripheralController::handle_app_registered(org_bluez_GattManager1* mgr) {
    FIBRE_LOG(D) << "application registered";
}

void BluezPeripheralController::handle_app_register_failed(org_bluez_GattManager1* mgr) {
    FIBRE_LOG(E) << "failed to register application";
}

void BluezPeripheralController::handle_app_unregistered(org_bluez_GattManager1* mgr) {
    FIBRE_LOG(D) << "application unregistered";
}

void BluezPeripheralController::handle_app_unregister_failed(org_bluez_GattManager1* mgr) {
    FIBRE_LOG(E) << "failed to unregister application";
}

