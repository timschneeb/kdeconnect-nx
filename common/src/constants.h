#pragma once

#include <cstdint>

// X-macro tables: X(enum_name, default_value)
// NOTE: Do not enum entry names after release! They are written to JSON.
//       If the order is changed, the API version must be incremented. IPC uses enum number values.

#define KDEC_BOOL_SETTINGS(X) \
    X(NotificationShowRemoteMessages, true)  \
    X(NotificationShowOnConnect, false) \
    X(NotificationShowIcon, true) \
    X(NotificationDynamicFontSize, false)

#define KDEC_INT_SETTINGS(X) \
    X(NotificationDuration, 4000) \
    X(MprisSeekStepSize, 10000) /* ms */ \
    X(NotificationFontSize, 22)

#define _KDEC_X_ENUM(name, def) name,

enum class KdecBoolSettingKey : uint8_t {
    KDEC_BOOL_SETTINGS(_KDEC_X_ENUM)
    KDEC_BOOL_SETTING_COUNT,
};

enum class KdecIntSettingKey : uint8_t {
    KDEC_INT_SETTINGS(_KDEC_X_ENUM)
    KDEC_INT_SETTING_COUNT,
};

#undef _KDEC_X_ENUM

namespace constants {
    constexpr unsigned short kMaxDevices     = 16;
    constexpr unsigned short kMaxCommands    = 256;
    constexpr unsigned short kMaxVolumeSinks = 16;
    constexpr unsigned short kMaxSettings =
        static_cast<unsigned short>(KdecBoolSettingKey::KDEC_BOOL_SETTING_COUNT) +
        static_cast<unsigned short>(KdecIntSettingKey::KDEC_INT_SETTING_COUNT);
}
