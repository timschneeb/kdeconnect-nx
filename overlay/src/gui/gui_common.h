#pragma once

#include <tesla.hpp>
#include <kdec/ipc.h>
#include <cstdio>
#include <string>

#include "../utils/symbols.h"

#define TEXT_COLOR tsl::gfx::Renderer::a(0xFFFF)
#define DESC_COLOR tsl::gfx::Renderer::a(tsl::Color{0xC, 0xC, 0xC, 0xF})

static constexpr tsl::Color kTransparent{0, 0, 0, 0};
#define kWhite  tsl::gfx::Renderer::a(tsl::Color{0xF, 0xF, 0xF, 0xF})
#define kDim    tsl::gfx::Renderer::a(tsl::Color{0x9, 0x9, 0x9, 0xF})
#define kFaint  tsl::gfx::Renderer::a(tsl::Color{0x5, 0x5, 0x5, 0xF})

inline std::string batteryStr(const KdecDeviceInfo& dev, const bool long_desc) {
    if (dev.battery_level < 0) return ""; // No battery
    const char* chargingStr = long_desc ? " (charging)" : "+";
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%%s", static_cast<int>(dev.battery_level), dev.is_charging ? chargingStr : "");
    return std::string(buf);
}

inline std::string statusStr(const KdecDeviceInfo& dev) {
    switch (dev.pair_state) {
        case DevicePairState::Paired:          return  dev.is_connected ? "Connected" : "Offline";
        case DevicePairState::RequestedByMe:   return "Pairing...";
        case DevicePairState::RequestedByPeer: return "Incoming pair request";
        default:                               return "Not paired";
    }
}

inline std::string batteryOrStatusStr(const KdecDeviceInfo& dev) {
    if (dev.pair_state == DevicePairState::Paired && dev.is_connected)
        return batteryStr(dev, false);
    return statusStr(dev);
}

inline std::string devIcon(const KdecDeviceInfo& dev) {
    switch (dev.type) {
        case DeviceType::Phone:
            return sym::devicePhone;
        case DeviceType::Tablet:
            return sym::deviceSwitchLite;
        case DeviceType::TV:
            return sym::deviceTv;
        default:
            return sym::devicePc;
    }
}

inline std::string devName(const KdecDeviceInfo& dev) {return dev.name; }

inline std::string devId(const KdecDeviceInfo& dev) { return dev.id; }
