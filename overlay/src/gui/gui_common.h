#pragma once

#include <tesla.hpp>
#include <kdec/ipc.h>
#include <cstdio>
#include <string>

#define TEXT_COLOR tsl::gfx::Renderer::a(0xFFFF)
#define DESC_COLOR tsl::gfx::Renderer::a(tsl::Color{0xC, 0xC, 0xC, 0xF})

inline std::string batteryStr(const KdecDeviceInfo& dev) {
    if (dev.battery_level < 0) return "N/A";
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%%s", static_cast<int>(dev.battery_level), dev.is_charging ? "+" : "");
    return buf;
}

inline const char* statusStr(const KdecDeviceInfo& dev) {
    switch (dev.pair_state) {
        case DevicePairState::Paired:          return dev.is_connected ? "Connected" : "Paired, offline";
        case DevicePairState::RequestedByMe:   return "Pairing...";
        case DevicePairState::RequestedByPeer: return "Incoming pair request";
        default:                               return "Not paired";
    }
}

inline std::string devName(const KdecDeviceInfo& dev) { return dev.name; }
inline std::string devId(const KdecDeviceInfo& dev)   { return dev.id;   }
