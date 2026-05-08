#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

struct NetworkPacket {
    std::string type;
    nlohmann::json body;

    [[nodiscard]] std::string serialize() const;
    static std::optional<NetworkPacket> parse(const std::string& line);
};

