#include "battery_plugin.h"
#include "../utils/logger.h"

// Hardcoded for PC prototype
static constexpr int kCharge = 72;
static constexpr bool kCharging = false;
static constexpr int kThresholdEvent = (kCharge <= 15) ? 1 : 0;

std::string BatteryPlugin::name() const { return "Battery Plugin"; }
std::string BatteryPlugin::description() const { return "Reports battery status."; }

std::vector<std::string> BatteryPlugin::supported_packet_types() const {
    return { PacketTypes::Battery, PacketTypes::BatteryRequest };
}

std::vector<std::string> BatteryPlugin::outgoing_packet_types() const {
    return { PacketTypes::Battery };
}

void BatteryPlugin::on_connected(bool paired) {
    if (paired) {
        send_status();
    }
}

bool BatteryPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type == PacketTypes::BatteryRequest) {
        send_status();
        return true;
    }
    if (np.type == PacketTypes::Battery) {
        int charge = np.body.value("currentCharge", -1);
        bool charging = np.body.value("isCharging", false);
        std::string msg = "[BATTERY] Remote: " + std::to_string(charge) + "%" + (charging ? " (charging)" : "");
        if (charge <= 15 && np.body.value("thresholdEvent", 0) == 1) msg += " LOW";
        Logger::info(msg);
        return true;
    }
    return false;
}

void BatteryPlugin::send_status() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::Battery;
    pkt.body = {
        {"currentCharge", kCharge},
        {"isCharging", kCharging},
        {"thresholdEvent", kThresholdEvent}
    };
    send_packet(pkt);
}
