#ifndef __FIBRE_BLUETOOTH_DISCOVERER_HPP
#define __FIBRE_BLUETOOTH_DISCOVERER_HPP

#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/dbus.hpp>
#include <fibre/channel_discoverer.hpp>
#include "../../dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp"
#include "../../dbus_interfaces/org.bluez.LEAdvertisingManager1.hpp"
#include "../../dbus_interfaces/org.bluez.GattManager1.hpp"

namespace fibre {

class BluetoothCentralSideDiscoverer : ChannelDiscoverer {
public:
    int init(Worker* worker, DBusConnectionWrapper* dbus);
    int deinit();
    int start_channel_discovery(interface_specs* interface_specs, void** discovery_ctx);
    int stop_channel_discovery(void* discovery_ctx);

private:
    using adapter_t = DBusRemoteObject<org_bluez_LEAdvertisingManager1, org_bluez_GattManager1>;

    int start_ble_adapter_monitor();
    int stop_ble_adapter_monitor();

    void handle_adapter_found(adapter_t* adapter);
    void handle_adapter_lost(adapter_t* adapter);
    void handle_ad_registered(org_bluez_LEAdvertisingManager1* mgr);

    Worker* worker_ = nullptr;
    DBusConnectionWrapper* dbus_ = nullptr;
    DBusRemoteObject<org_freedesktop_DBus_ObjectManager> bluez_root_obj_{{nullptr, "", ""}};
    DBusDiscoverer<org_bluez_LEAdvertisingManager1, org_bluez_GattManager1> dbus_discoverer_{};
    DBusObjectPath ad_obj_path{};
    int n_discovery_requests = 0;

    member_closure_t<decltype(&BluetoothCentralSideDiscoverer::handle_adapter_found)> handle_adapter_found_obj_{&BluetoothCentralSideDiscoverer::handle_adapter_found, this};
    member_closure_t<decltype(&BluetoothCentralSideDiscoverer::handle_adapter_lost)> handle_adapter_lost_obj_{&BluetoothCentralSideDiscoverer::handle_adapter_lost, this};
    member_closure_t<decltype(&BluetoothCentralSideDiscoverer::handle_ad_registered)> handle_ad_registered_obj_{&BluetoothCentralSideDiscoverer::handle_ad_registered, this};
};


}

#endif // __FIBRE_BLUETOOTH_DISCOVERER_HPP