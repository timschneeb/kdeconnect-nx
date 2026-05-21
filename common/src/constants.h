#pragma once

#include <cstdint>

enum class KdecBoolSettingKey : uint8_t {
    NotificationShowRemote,
    NotificationShowOnConnect,

    KDEC_BOOL_SETTING_COUNT,
};

enum class KdecIntSettingKey : uint8_t {
    NotificationDuration,

    KDEC_INT_SETTING_COUNT
};

namespace constants {
    constexpr unsigned short kMaxDevices     = 16;
    constexpr unsigned short kMaxCommands    = 256;
    constexpr unsigned short kMaxVolumeSinks = 16;
    constexpr unsigned short kMaxSettings =
        static_cast<unsigned short>(KdecBoolSettingKey::KDEC_BOOL_SETTING_COUNT) +
        static_cast<unsigned short>(KdecIntSettingKey::KDEC_INT_SETTING_COUNT);
}