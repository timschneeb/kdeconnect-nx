#include "notification_plugin.h"
#include "../utils/logger.h"

std::string NotificationPlugin::name() const { return "Notification Plugin"; }
std::string NotificationPlugin::description() const { return "Receives notifications from remote devices."; }

std::vector<std::string> NotificationPlugin::supported_packet_types() const {
    return { PacketTypes::Notification };
}

std::vector<std::string> NotificationPlugin::outgoing_packet_types() const {
    return { PacketTypes::NotificationRequest };
}

void NotificationPlugin::on_connected(bool paired) {
    if (paired) {
        request_active_notifications();
    }
}

bool NotificationPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::Notification) return false;

    if (np.body.value("isCancel", false)) {
        Logger::info("[NOTIFICATION] Dismissed: " + np.body.value("id", ""));
        return true;
    }

    bool silent = np.body.value("silent", false);
    std::string app = np.body.value("appName", "Unknown");
    std::string title = np.body.value("title", "");
    std::string text = np.body.value("text", "");

    if (!silent) {
        std::string msg = "[NOTIFICATION] " + app + ": " + title;
        if (!text.empty()) msg += " - " + text;
        Logger::info(msg);
    }
    return true;
}

void NotificationPlugin::request_active_notifications() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::NotificationRequest;
    pkt.body = { {"request", true} };
    send_packet(pkt);
}
