#pragma once

#include <cstdint>
#include <string>
#include <variant>

#define KDEC_IPC_API_VERSION 1
#define KDEC_IPC_SERVICE_NAME "kdec:srv"

enum KdecIpcCmd {
    KdecIpcCmd_GetApiVersion    = 0,
    KdecIpcCmd_GetDeviceCount   = 1,
    KdecIpcCmd_GetDevices       = 2,
    KdecIpcCmd_RequestPair      = 3,
    KdecIpcCmd_AcceptPair       = 4,
    KdecIpcCmd_RejectPair       = 5,
    KdecIpcCmd_Unpair           = 6,
    KdecIpcCmd_Ping             = 7,
    KdecIpcCmd_GetMediaInfo     = 8,
    KdecIpcCmd_SendMediaAction  = 9,
    KdecIpcCmd_GetCommandList   = 10,
    KdecIpcCmd_RunCommand       = 11,
    KdecIpcCmd_ReadSetting      = 12,
    KdecIpcCmd_WriteSetting     = 13,
    KdecIpcCmd_GetAllSettings   = 14,
};

enum class DevicePairState : uint8_t {
    None            = 0,
    RequestedByMe   = 1,
    RequestedByPeer = 2,
    Paired          = 3,
};

enum class KdecMediaAction : uint8_t {
    Play        = 0,
    Pause       = 1,
    PlayPause   = 2,
    Stop        = 3,
    Next        = 4,
    Previous    = 5,
    SetVolume   = 6,
    Seek        = 7,
    SetPosition = 8,
};

enum class KdecSettingType : uint8_t {
    Bool   = 0,
    Int    = 1,
    String = 2,
};

struct KdecDeviceInfo {
    std::string id;
    std::string name;
    DevicePairState pair_state;
    bool is_connected;
    bool supports_find_my_phone;
    int8_t battery_level; // -1 = unavailable
};

struct KdecMediaInfo {
    std::string device_id;
    std::string player;
    std::string title;
    std::string artist;
    std::string album;
    bool is_playing;
    bool can_play;
    bool can_pause;
    bool can_go_next;
    bool can_go_previous;
    bool can_seek;
    int32_t volume;
    int64_t position;
    int64_t length;
};

struct KdecCommandEntry {
    std::string id;
    std::string name;
};

struct KdecSettingEntry {
    std::string key;
    KdecSettingType type;
    std::variant<bool, int32_t, std::string> value;
};
