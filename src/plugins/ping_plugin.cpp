#include "ping_plugin.h"
#include "../logger.h"

std::string PingPlugin::name() const {
    return "Ping Plugin";
}

std::string PingPlugin::description() const {
    return "Sends and receives pings.";
}

std::vector<std::string> PingPlugin::supported_packet_types() const {
    return { PacketTypes::Ping };
}

std::vector<std::string> PingPlugin::outgoing_packet_types() const {
    return { PacketTypes::Ping };
}

bool PingPlugin::on_packet_received(const NetworkPacket& np) {
    std::string msg = "Ping!";
    if (np.body.contains("message") && np.body["message"].is_string()) {
        msg = np.body["message"].get<std::string>();
    }
    
    Logger::info("[PING RECEIVED] From " + device_id_ + ": " + msg);
    return true;
}

void PingPlugin::ping(const std::string &message) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::Ping;
    pkt.body = nlohmann::json::object();
    if (!message.empty()) {
        pkt.body["message"] = message;
    }

    send_packet(pkt);
}
