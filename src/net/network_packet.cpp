#include "../network_packet.h"

#include <chrono>

std::string NetworkPacket::serialize() const {
    nlohmann::json root;
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    root["id"] = now;
    root["type"] = type;
    root["body"] = body;
    return root.dump() + "\n";
}

std::optional<NetworkPacket> NetworkPacket::parse(const std::string& line) {
    try {
        nlohmann::json root = nlohmann::json::parse(line);
        if (!root.contains("type") || !root.contains("body")) {
            return std::nullopt;
        }
        NetworkPacket pkt;
        pkt.type = root.at("type").get<std::string>();
        pkt.body = root.at("body");
        if (root.contains("payloadSize") && root["payloadSize"].is_number()) {
            pkt.payload_size = root["payloadSize"].get<int64_t>();
            if (root.contains("payloadTransferInfo") && root["payloadTransferInfo"].is_object()) {
                pkt.payload_port = root["payloadTransferInfo"].value("port", -1);
            }
        }
        return pkt;
    } catch (...) {
        return std::nullopt;
    }
}

