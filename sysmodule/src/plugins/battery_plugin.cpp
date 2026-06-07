#include "battery_plugin.h"
#include "notification_plugin.h"
#include "utils/logger.h"
#include "utils/settings_store.h"
#include <chrono>
#include <string>

#define ALLOW_MULTIPLE_THRESHOLD_EVENTS

#ifdef __SWITCH__
#include <switch.h>
#endif

std::string BatteryPlugin::name() const { return "Battery Plugin"; }
std::string BatteryPlugin::description() const { return "Reports battery status."; }

std::vector<std::string> BatteryPlugin::supported_packet_types() const {
    return { PacketTypes::Battery, PacketTypes::BatteryRequest };
}

std::vector<std::string> BatteryPlugin::outgoing_packet_types() const {
    return { PacketTypes::Battery };
}

void BatteryPlugin::on_create() {
#ifdef __SWITCH__
    psm_initialized_ = R_SUCCEEDED(psmInitialize());
#endif
}

BatteryPlugin::~BatteryPlugin() {
#ifdef __SWITCH__
    if (psm_initialized_) psmExit();
#endif
}

bool BatteryPlugin::read_hardware(int32_t& charge, bool& charging) const {
#ifdef __SWITCH__
    if (!psm_initialized_) return false;
    u32 pct = 0;
    if (R_FAILED(psmGetBatteryChargePercentage(&pct))) return false;
    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_FAILED(psmGetChargerType(&charger))) return false;
    charge = pct;
    charging = (charger != PsmChargerType_Unconnected);
    return true;
#else
    charge = 100;
    charging = false;
    return true;
#endif
}

void BatteryPlugin::on_connected(const bool paired) {
    if (!paired) return;
    active_ = true;
    notified_low_ = false;

    int32_t charge = 0;
    bool charging = false;
    if (read_hardware(charge, charging)) {
        cached_charge_.store(charge);
        cached_charging_.store(charging);
        cache_valid_.store(true);
        send_status();
    }
    last_poll_ = std::chrono::steady_clock::now();
}

void BatteryPlugin::process_events() {
    if (!active_) return;

    const auto now = std::chrono::steady_clock::now();
    if (now - last_poll_ < std::chrono::seconds(10)) return;
    last_poll_ = now;

    int32_t charge = 0;
    bool charging = false;
    if (!read_hardware(charge, charging)) return;

    if (charge != cached_charge_.load() || charging != cached_charging_.load()) {
        cached_charge_.store(charge);
        cached_charging_.store(charging);
        send_status();
    }
}

bool BatteryPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type == PacketTypes::BatteryRequest) {
        send_status();
        return true;
    }
    if (np.type == PacketTypes::Battery) {
        int remote_charge = np.body.value("currentCharge", -1);
        bool remote_charging = np.body.value("isCharging", false);
        bool is_low = np.body.value("thresholdEvent", 0) == 1;

        cached_remote_charge_ = remote_charge;
        cached_remote_charging_ = remote_charging;

#ifdef ALLOW_MULTIPLE_THRESHOLD_EVENTS
        if (!remote_charging && is_low &&
                SettingsStore::get(KdecBoolSettingKey::NotificationShowOnBatteryLow)) {
#else
        if (!remote_charging && is_low && !notified_low_ &&
                SettingsStore::get(KdecBoolSettingKey::NotificationShowOnBatteryLow)) {
#endif
            notified_low_ = true;
            auto session = provider_ ? provider_->device(device_id_) : nullptr;
            const std::string dev_name = session ? session->info.name : device_id_;
            NotificationPlugin::post_notification(
                "kdeconnect",
                dev_name,
                "Low Battery: " + std::to_string(remote_charge) + "%",
                "battery_low_" + device_id_,
                SettingsStore::get(KdecIntSettingKey::NotificationDuration));
        } else if (remote_charging || remote_charge > 15) {
            notified_low_ = false;
        }

        return true;
    }
    return false;
}

void BatteryPlugin::send_status() const {
    int32_t charge = 0;
    bool charging = false;

    if (cache_valid_.load()) {
        charge = cached_charge_.load();
        charging = cached_charging_.load();
    } else if (!read_hardware(charge, charging)) {
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Battery;
    pkt.body.set("currentCharge",  (int)charge)
            .set("isCharging",     charging)
            .set("thresholdEvent", (!charging && charge <= 15u) ? 1 : 0);
    send_packet(pkt);
}

void BatteryPlugin::read_remote_state(int8_t &charge, bool &charging) const {
    charge = cached_remote_charge_.load();
    charging = cached_remote_charging_.load();
}
