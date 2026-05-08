#include "network_packet.h"

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
        return pkt;
    } catch (...) {
        return std::nullopt;
    }
}

