#pragma once

#include <optional>
#include <string>
#include <vector>

#include "json_body.h"

namespace PacketTypes {
    constexpr auto Identity = "kdeconnect.identity";
    constexpr auto Pair = "kdeconnect.pair";
    constexpr auto Ping = "kdeconnect.ping";
    constexpr auto Battery = "kdeconnect.battery";
    constexpr auto BatteryRequest = "kdeconnect.battery.request";
    constexpr auto Notification = "kdeconnect.notification";
    constexpr auto NotificationRequest = "kdeconnect.notification.request";
    constexpr auto FindMyPhoneRequest = "kdeconnect.findmyphone.request";
    constexpr auto Mpris = "kdeconnect.mpris";
    constexpr auto MprisRequest = "kdeconnect.mpris.request";
    constexpr auto SystemVolume = "kdeconnect.systemvolume";
    constexpr auto SystemVolumeRequest = "kdeconnect.systemvolume.request";
    constexpr auto ShareRequest = "kdeconnect.share.request";
    constexpr auto RunCommand = "kdeconnect.runcommand";
    constexpr auto RunCommandRequest = "kdeconnect.runcommand.request";
    constexpr auto MousepadRequest = "kdeconnect.mousepad.request";
    constexpr auto MousepadEcho = "kdeconnect.mousepad.echo";
    constexpr auto MousepadKeyboardState = "kdeconnect.mousepad.keyboardstate";
}

struct NetworkPacket {
    std::string type;
    JsonBody body;

    // Populated by parse() when the sender advertises a binary payload.
    int64_t payload_size = -1;
    int     payload_port = -1;

    // Payload to be attached
    std::vector<uint8_t> send_payload;

    [[nodiscard]] bool has_payload() const { return payload_port > 0 && payload_size > 0;}
    [[nodiscard]] std::string serialize() const;
    static std::optional<NetworkPacket> parse(const std::string& line);
};

