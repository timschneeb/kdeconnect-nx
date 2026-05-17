#pragma once

#include <stdint.h>

#define KDEC_IPC_API_VERSION 1
#define KDEC_IPC_SERVICE_NAME "kdec:srv"

enum KdecIpcCmd {
    KdecIpcCmd_GetApiVersion = 0,
    KdecIpcCmd_GetDeviceCount = 1,
    KdecIpcCmd_GetDevices = 2,
    KdecIpcCmd_RequestPair = 3,
    KdecIpcCmd_AcceptPair = 4,
    KdecIpcCmd_RejectPair = 5,
    KdecIpcCmd_Unpair = 6,
};

#define KDEC_MAX_DEVICE_NAME 32
#define KDEC_MAX_DEVICE_ID 64

enum class DevicePairState : uint8_t {
    None = 0,
    RequestedByMe = 1,
    RequestedByPeer = 2,
    Paired = 3,
};

struct KdecDeviceInfo {
    char id[KDEC_MAX_DEVICE_ID];
    char name[KDEC_MAX_DEVICE_NAME];
    DevicePairState pair_state;
    bool is_connected;
};
