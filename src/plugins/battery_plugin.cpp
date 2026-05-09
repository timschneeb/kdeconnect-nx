#include "battery_plugin.h"
#include "../utils/logger.h"
#include <chrono>
#include <thread>

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
    running_.store(false);
    if (poll_thread_.joinable()) poll_thread_.join();
#ifdef __SWITCH__
    if (psm_initialized_) psmExit();
#endif
}

bool BatteryPlugin::read_hardware(uint32_t& charge, bool& charging) const {
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
    running_.store(true);
    poll_thread_ = std::thread(&BatteryPlugin::poll_loop, this);
}

void BatteryPlugin::poll_loop() {
    uint32_t charge = 0;
    bool charging = false;

    if (read_hardware(charge, charging)) {
        cached_charge_.store(charge);
        cached_charging_.store(charging);
        cache_valid_.store(true);
        send_status();
    }

    while (running_.load()) {
        // 10 s in 100 ms ticks so the thread exits quickly on shutdown
        for (int i = 0; i < 100 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!running_.load()) break;

        uint32_t new_charge = charge;
        bool new_charging = charging;
        if (read_hardware(new_charge, new_charging)) {
            if (new_charge != charge || new_charging != charging) {
                charge = new_charge;
                charging = new_charging;
                cached_charge_.store(charge);
                cached_charging_.store(charging);
                send_status();
            }
        }
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
        std::string msg = "[BATTERY] Remote: " + std::to_string(remote_charge) + "%" +
                          (remote_charging ? " (charging)" : "");
        if (remote_charge <= 15 && np.body.value("thresholdEvent", 0) == 1) msg += " LOW";
        Logger::info(msg);
        return true;
    }
    return false;
}

void BatteryPlugin::send_status() const {
    uint32_t charge = cached_charge_.load();
    bool charging = cached_charging_.load();

    if (!cache_valid_.load()) {
        // Poll thread hasn't run yet; read directly.
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
