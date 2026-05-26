
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include <switch.h>

#include "ipc.h"
#include "ipc_client.h"

#include "logger.h"

static Service g_kdecSrv;
static std::atomic<size_t> g_refCnt;

bool kdecIpcIsConnected() {
    return serviceIsActive(&g_kdecSrv);
}

bool kdecIpcRunning() {
    Handle handle;
    bool running = R_FAILED(smRegisterService(&handle, smEncodeName(KDEC_IPC_SERVICE_NAME), false, 1));

    if (!running) {
        smUnregisterService(smEncodeName(KDEC_IPC_SERVICE_NAME));
    }

    return running;
}

Result kdecIpcInitialize() {
    Result rc = 0;

    g_refCnt++;

    if (serviceIsActive(&g_kdecSrv))
        return 0;

    rc = smGetService(&g_kdecSrv, KDEC_IPC_SERVICE_NAME);

    if (R_FAILED(rc)) {
        g_refCnt--;
        serviceClose(&g_kdecSrv);
    }

    return rc;
}

void kdecIpcExit() {
    if (g_refCnt == 0) return; // never initialized; avoid size_t underflow
    if (--g_refCnt == 0) {
        serviceClose(&g_kdecSrv);
    }
}

Result kdecIpcGetApiVersion(uint32_t& out) {
    return serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetApiVersion, out);
}

Result kdecIpcGetDeviceCount(uint32_t& out) {
    return serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetDeviceCount, out);
}

Result kdecIpcGetDevices(std::vector<KdecDeviceInfo>& out) {
    static constexpr uint32_t kMaxDevices = 16;
    static KdecDeviceInfo buf[kMaxDevices];

    uint32_t count = 0;
    Result rc = serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetDevices, count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, sizeof(buf) }},
    );
    if (R_FAILED(rc)) return rc;

    if (count > kMaxDevices) count = kMaxDevices;
    out.assign(buf, buf + count);
    return 0;
}

Result kdecIpcRequestPair(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_RequestPair, wire);
}

Result kdecIpcAcceptPair(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_AcceptPair, wire);
}

Result kdecIpcRejectPair(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_RejectPair, wire);
}

Result kdecIpcUnpair(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_Unpair, wire);
}

Result kdecIpcPing(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_Ping, wire);
}

Result kdecIpcRing(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_Ring, wire);
}

Result kdecIpcGetMediaInfo(const std::string& device_id, KdecMediaInfo& out) {
    KdecWireDeviceId id_wire{};
    strncpy(id_wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    uint32_t found = 0;
    Result rc = serviceDispatchInOut(&g_kdecSrv, KdecIpcCmd_GetMediaInfo, id_wire, found,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ &out, sizeof(KdecMediaInfo) }},
    );
    if (R_FAILED(rc)) return rc;
    return found ? 0 : MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

Result kdecIpcSendMediaAction(const std::string& device_id, KdecMediaAction action, int64_t value) {
    KdecWireSendMediaAction wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    wire.action = static_cast<uint8_t>(action);
    wire.value  = value;
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_SendMediaAction, wire);
}

Result kdecIpcGetCommandList(const std::string& device_id, std::vector<KdecCommandEntry>& out) {
    static KdecCommandEntry buf[constants::kMaxCommands];

    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);

    uint32_t count = 0;
    Result rc = serviceDispatchInOut(&g_kdecSrv, KdecIpcCmd_GetCommandList, wire, count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, sizeof(buf) }},
    );
    if (R_FAILED(rc)) return rc;

    if (count > constants::kMaxCommands) count = constants::kMaxCommands;
    out.assign(buf, buf + count);
    return 0;
}

Result kdecIpcRunCommand(const std::string& device_id, const std::string& command_id) {
    KdecWireRunCommand wire{};
    strncpy(wire.device_id,  device_id.c_str(),  KDEC_DEVICE_ID_MAX  - 1);
    strncpy(wire.command_id, command_id.c_str(), KDEC_COMMAND_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_RunCommand, wire);
}

Result kdecIpcReadBoolSetting(KdecBoolSettingKey key, bool& out) {
    return serviceDispatchInOut(&g_kdecSrv, KdecIpcCmd_ReadBoolSetting, key, out);
}

Result kdecIpcWriteBoolSetting(KdecBoolSettingKey key, bool value) {
    KdecWireWriteBoolSetting wire{key, value};
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_WriteBoolSetting, wire);
}

Result kdecIpcReadIntSetting(KdecIntSettingKey key, int32_t& out) {
    return serviceDispatchInOut(&g_kdecSrv, KdecIpcCmd_ReadIntSetting, key, out);
}

Result kdecIpcWriteIntSetting(KdecIntSettingKey key, int32_t value) {
    KdecWireWriteIntSetting wire{key, value};
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_WriteIntSetting, wire);
}

Result kdecIpcGetVolumeSinks(const std::string& device_id, std::vector<KdecVolumeSinkInfo>& out) {
    static KdecVolumeSinkInfo buf[constants::kMaxVolumeSinks];

    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);

    uint32_t count = 0;
    Result rc = serviceDispatchInOut(&g_kdecSrv, KdecIpcCmd_GetVolumeSinks, wire, count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, sizeof(buf) }},
    );
    if (R_FAILED(rc)) return rc;

    if (count > constants::kMaxVolumeSinks) count = constants::kMaxVolumeSinks;
    out.assign(buf, buf + count);
    return 0;
}

Result kdecIpcSetVolumeSink(const std::string& device_id, const std::string& sink_name, int32_t volume, bool muted, bool is_default_output) {
    KdecWireSetVolumeSink wire{};
    strncpy(wire.device_id,  device_id.c_str(),  KDEC_DEVICE_ID_MAX - 1);
    strncpy(wire.sink_name,  sink_name.c_str(),   KDEC_SINK_NAME_MAX - 1);
    wire.volume            = volume;
    wire.muted             = muted;
    wire.is_default_output = is_default_output;
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_SetVolumeSink, wire);
}

Result kdecIpcSendScreenshot(const std::string& device_id) {
    KdecWireDeviceId wire{};
    strncpy(wire.device_id, device_id.c_str(), KDEC_DEVICE_ID_MAX - 1);
    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_SendScreenshot, wire);
}

Result kdecIpcGetAllSettings(std::vector<KdecWireSettingEntry>& out) {
    static KdecWireSettingEntry buf[constants::kMaxSettings];

    uint32_t count = 0;
    Result rc = serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetAllSettings, count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, sizeof(buf) }},
    );
    if (R_FAILED(rc)) return rc;

    if (count > constants::kMaxSettings) {
        Logger::error("GetAllSettings returned more entries than expected: %u (max %u)", count, constants::kMaxSettings);
        count = constants::kMaxSettings;
    }
    out.assign(buf, buf + count);
    return 0;
}