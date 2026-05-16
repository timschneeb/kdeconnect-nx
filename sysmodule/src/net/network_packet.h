#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace PacketTypes {
    constexpr const char* Identity = "kdeconnect.identity";
    constexpr const char* Pair = "kdeconnect.pair";
    constexpr const char* Ping = "kdeconnect.ping";
    constexpr const char* Battery = "kdeconnect.battery";
    constexpr const char* BatteryRequest = "kdeconnect.battery.request";
    constexpr const char* Notification = "kdeconnect.notification";
    constexpr const char* NotificationRequest = "kdeconnect.notification.request";
    constexpr const char* FindMyPhoneRequest = "kdeconnect.findmyphone.request";
    constexpr const char* Mpris = "kdeconnect.mpris";
    constexpr const char* MprisRequest = "kdeconnect.mpris.request";
    constexpr const char* SystemVolume = "kdeconnect.systemvolume";
    constexpr const char* SystemVolumeRequest = "kdeconnect.systemvolume.request";
    constexpr const char* ShareRequest = "kdeconnect.share.request";
    constexpr const char* RunCommand = "kdeconnect.runcommand";
    constexpr const char* RunCommandRequest = "kdeconnect.runcommand.request";
    constexpr const char* MousepadRequest = "kdeconnect.mousepad.request";
    constexpr const char* MousepadEcho = "kdeconnect.mousepad.echo";
    constexpr const char* MousepadKeyboardState = "kdeconnect.mousepad.keyboardstate";
}

struct NetworkPacket {
    std::string type;
    nlohmann::json body;

    // Populated by parse() when the sender advertises a binary payload.
    int64_t payload_size = -1;
    int     payload_port = -1;

    // Filled in by the transport layer before the packet reaches plugins.
    std::vector<uint8_t> payload;

    [[nodiscard]] std::string serialize() const;
    static std::optional<NetworkPacket> parse(const std::string& line);
};

