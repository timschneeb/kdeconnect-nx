#include "battery_plugin.h"
#include "utils/logger.h"
#include <chrono>

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

void BatteryPlugin::on_connected(bool paired) {
    if (!paired) return;
    active_ = true;

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
        bool is_low = remote_charge <= 15 && np.body.value("thresholdEvent", 0) == 1;
        Logger::info("Remote: %d%%%s%s", remote_charge, remote_charging ? " (charging)" : "", is_low ? " LOW" : "");

        cached_remote_charge_ = remote_charge;
        cached_remote_charging_ = remote_charging;
        return true;
    }
    return false;
}

void BatteryPlugin::send_status() const {
    int32_t charge = cached_charge_.load();
    bool charging = cached_charging_.load();

    if (!cache_valid_.load()) {
        read_hardware(charge, charging);
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Battery;
    pkt.body = {
        {"currentCharge", static_cast<int>(charge)},
        {"isCharging", charging},
        {"thresholdEvent", (!charging && charge <= 15u) ? 1 : 0}
    };
    send_packet(pkt);
}

void BatteryPlugin::read_remote_state(int8_t &charge, bool &charging) const {
    charge = cached_remote_charge_.load();
    charging = cached_remote_charging_.load();
}
