#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace PacketTypes {
    constexpr const char* Identity = "kdeconnect.identity";
    constexpr const char* Pair = "kdeconnect.pair";
    constexpr const char* Ping = "kdeconnect.ping";
}

struct NetworkPacket {
    std::string type;
    nlohmann::json body;

    [[nodiscard]] std::string serialize() const;
    static std::optional<NetworkPacket> parse(const std::string& line);
};

