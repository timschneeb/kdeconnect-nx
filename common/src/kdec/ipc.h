#pragma once

#include <cstdint>
#include "../constants.h"

#define KDEC_IPC_API_VERSION  1
#define KDEC_IPC_SERVICE_NAME "kdec:srv"

#define KDEC_DEVICE_ID_MAX    64
#define KDEC_DEVICE_NAME_MAX  128
#define KDEC_PLAYER_MAX       256
#define KDEC_TITLE_MAX        256
#define KDEC_ARTIST_MAX       256
#define KDEC_ALBUM_MAX        256
#define KDEC_COMMAND_ID_MAX   64
#define KDEC_COMMAND_NAME_MAX 64
#define KDEC_SINK_NAME_MAX    64
#define KDEC_SINK_DESC_MAX    128

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
    KdecIpcCmd_ReadBoolSetting  = 12,
    KdecIpcCmd_WriteBoolSetting = 13,
    KdecIpcCmd_ReadIntSetting   = 14,
    KdecIpcCmd_WriteIntSetting  = 15,
    KdecIpcCmd_GetAllSettings   = 16,
    KdecIpcCmd_Ring             = 17,
    KdecIpcCmd_GetVolumeSinks   = 18,
    KdecIpcCmd_SetVolumeSink    = 19,
    KdecIpcCmd_SendScreenshot   = 20,
    KdecIpcCmd_GetMemoryInfo    = 21,
    KdecIpcCmd_SendTestNotification = 22,
    KdecIpcCmd_SendBroadcast    = 23,
    KdecIpcCmd_GetVersionInfo   = 24,
};

enum class DevicePairState : uint8_t {
    None            = 0,
    RequestedByMe   = 1,
    RequestedByPeer = 2,
    Paired          = 3,
};

enum class DeviceType : uint8_t {
    Desktop = 0,
    Laptop  = 1,
    Phone   = 2,
    Tablet  = 3,
    TV      = 4,
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

// ---------- Application / wire structs ----------
struct KdecDeviceInfo {
    char            id[KDEC_DEVICE_ID_MAX];
    char            name[KDEC_DEVICE_NAME_MAX];
    DeviceType      type;
    DevicePairState pair_state;
    bool            is_connected;
    int8_t          battery_level; // -1 = no battery
    bool            is_charging;
    bool            supports_find_my_phone;
    bool            supports_commands;
    bool            supports_volume_sinks;
    bool            supports_mpris_remote;
    bool            supports_share;
} __attribute__ ((aligned (16)));

struct KdecMediaInfo {
    char    device_id[KDEC_DEVICE_ID_MAX];
    char    player[KDEC_PLAYER_MAX];
    char    title[KDEC_TITLE_MAX];
    char    artist[KDEC_ARTIST_MAX];
    char    album[KDEC_ALBUM_MAX];
    int64_t position;       // current position in ms
    int64_t length;         // total duration in ms
    int32_t volume;         // 0–100
    bool    is_playing;
    bool    can_play;
    bool    can_pause;
    bool    can_go_next;
    bool    can_go_previous;
    bool    can_seek;
    char    album_art_hash[17]; // 16-char lowercase hex FNV hash of albumArtUrl; empty if none
} __attribute__((aligned(16)));

struct KdecCommandEntry {
    char id[KDEC_COMMAND_ID_MAX];
    char name[KDEC_COMMAND_NAME_MAX];
} __attribute__((aligned(16)));

struct KdecVolumeSinkInfo {
    char    name[KDEC_SINK_NAME_MAX];
    char    description[KDEC_SINK_DESC_MAX];
    int32_t volume;
    bool    is_muted;
    bool    is_default_output;
} __attribute__((aligned(16)));
static_assert(sizeof(KdecVolumeSinkInfo) == 208);

// ------ Small structs that are sent inline ------

struct KdecMemoryInfo {
    size_t proc_used_kb;
    size_t socket_tmem_kb;
    size_t heap_used_kb;
    size_t heap_total_kb;
    size_t heap_max_kb;
} __attribute__((aligned(16)));
static_assert(sizeof(KdecMemoryInfo) == 48);

#define KDEC_VERSION_MAX     32
#define KDEC_GIT_COMMIT_MAX  16

struct KdecVersionInfo {
    char version[KDEC_VERSION_MAX];
    char git_commit[KDEC_GIT_COMMIT_MAX];
    bool is_debug;
} __attribute__((aligned(16)));
static_assert(sizeof(KdecVersionInfo) == 64);

struct KdecWireDeviceId {
    char device_id[KDEC_DEVICE_ID_MAX];
};
static_assert(sizeof(KdecWireDeviceId) == 64);

struct KdecWireRunCommand {
    char device_id[KDEC_DEVICE_ID_MAX];
    char command_id[KDEC_COMMAND_ID_MAX];
};
static_assert(sizeof(KdecWireRunCommand) == 128);

struct KdecWireWriteBoolSetting {
    KdecBoolSettingKey key;
    bool               value;
};
static_assert(sizeof(KdecWireWriteBoolSetting) == 2);

struct KdecWireWriteIntSetting {
    KdecIntSettingKey key;
    int32_t           value;
};
static_assert(sizeof(KdecWireWriteIntSetting) == 8);

// Used by GetAllSettings output buffer only
struct KdecWireSettingEntry {
    uint8_t key;
    union {
        bool as_bool;
        int32_t as_int;
    } value;
} __attribute__((aligned(16)));
static_assert(sizeof(KdecWireSettingEntry) == 16);

struct KdecWireSendMediaAction {
    char    device_id[KDEC_DEVICE_ID_MAX]; // target device
    uint8_t action; // KdecMediaAction
    // 7 bytes implicit padding before int64_t
    int64_t value;
} __attribute__((aligned(16)));
static_assert(sizeof(KdecWireSendMediaAction) == 80);

struct KdecWireSetVolumeSink {
    char    device_id[KDEC_DEVICE_ID_MAX];
    char    sink_name[KDEC_SINK_NAME_MAX];
    int32_t volume;
    bool    muted;
    bool    is_default_output;
} __attribute__((aligned(16)));
static_assert(sizeof(KdecWireSetVolumeSink) == 144);
