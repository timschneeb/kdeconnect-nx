#include <switch.h>
#include <atomic>
#include <cstring>

#include "ipc.h"
#include "ipc_buffer.h"
#include "ipc_client.h"

static Service g_kdecSrv;
static std::atomic<size_t> g_refCnt;

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
    static constexpr size_t kBufSize = 64 * 1024;
    uint8_t buf[kBufSize];

    uint32_t count = 0;
    Result rc = serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetDevices, count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, kBufSize }},
    );

    if (R_FAILED(rc)) return rc;

    out.clear();
    IpcReader rd(buf, kBufSize);
    for (uint32_t i = 0; i < count; i++) {
        if (!rd.ok()) break;
        KdecDeviceInfo info;
        info.id                    = rd.read_string();
        info.name                  = rd.read_string();
        info.pair_state            = static_cast<DevicePairState>(rd.read<uint8_t>());
        info.is_connected          = rd.read<bool>();
        info.supports_find_my_phone = rd.read<bool>();
        info.battery_level         = rd.read<int8_t>();
        if (rd.ok()) out.push_back(std::move(info));
    }
    return 0;
}

Result kdecIpcRequestPair(const std::string& device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_RequestPair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id.c_str(), device_id.size() + 1 }},
    );
}

Result kdecIpcAcceptPair(const std::string& device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_AcceptPair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id.c_str(), device_id.size() + 1 }},
    );
}

Result kdecIpcRejectPair(const std::string& device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_RejectPair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id.c_str(), device_id.size() + 1 }},
    );
}

Result kdecIpcUnpair(const std::string& device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_Unpair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id.c_str(), device_id.size() + 1 }},
    );
}

Result kdecIpcPing(const std::string& device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_Ping,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id.c_str(), device_id.size() + 1 }},
    );
}

Result kdecIpcGetMediaInfo(KdecMediaInfo& out) {
    static constexpr size_t kBufSize = 4096;
    uint8_t buf[kBufSize];

    uint32_t bytes_written = 0;
    Result rc = serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetMediaInfo, bytes_written,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, kBufSize }},
    );

    if (R_FAILED(rc)) return rc;

    IpcReader rd(buf, bytes_written);
    out.device_id       = rd.read_string();
    out.player          = rd.read_string();
    out.title           = rd.read_string();
    out.artist          = rd.read_string();
    out.album           = rd.read_string();
    out.is_playing      = rd.read<bool>();
    out.can_play        = rd.read<bool>();
    out.can_pause       = rd.read<bool>();
    out.can_go_next     = rd.read<bool>();
    out.can_go_previous = rd.read<bool>();
    out.can_seek        = rd.read<bool>();
    out.volume          = rd.read<int32_t>();
    out.position        = rd.read<int64_t>();
    out.length          = rd.read<int64_t>();

    return rd.ok() ? 0 : MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

Result kdecIpcSendMediaAction(KdecMediaAction action, int64_t value) {
    KdecWireSendMediaAction wire{};
    wire.action = static_cast<uint8_t>(action);
    wire.value  = value;

    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_SendMediaAction, wire);
}

Result kdecIpcGetCommandList(const std::string& device_id, std::vector<KdecCommandEntry>& out) {
    static constexpr size_t kBufSize = 32 * 1024;
    uint8_t buf[kBufSize];

    uint32_t count = 0;
    Result rc = serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetCommandList, count,
        .buffer_attrs = {
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
        },
        .buffers = {
            { device_id.c_str(), device_id.size() + 1 },
            { buf, kBufSize },
        },
    );

    if (R_FAILED(rc)) return rc;

    out.clear();
    IpcReader rd(buf, kBufSize);
    for (uint32_t i = 0; i < count; i++) {
        if (!rd.ok()) break;
        KdecCommandEntry entry;
        entry.id   = rd.read_string();
        entry.name = rd.read_string();
        if (rd.ok()) out.push_back(std::move(entry));
    }
    return 0;
}

Result kdecIpcRunCommand(const std::string& device_id, const std::string& command_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_RunCommand,
        .buffer_attrs = {
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
        },
        .buffers = {
            { device_id.c_str(), device_id.size() + 1 },
            { command_id.c_str(), command_id.size() + 1 },
        },
    );
}

Result kdecIpcReadSetting(const std::string& key, KdecSettingType type, KdecSettingEntry& out) {
    static constexpr size_t kBufSize = 512;
    uint8_t buf[kBufSize];

    KdecWireSettingType wire{ static_cast<uint32_t>(type) };

    // serviceDispatchIn doesn't support buffers directly; use the raw form
    Result rc = serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_ReadSetting, wire,
        .buffer_attrs = {
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
        },
        .buffers = {
            { key.c_str(), key.size() + 1 },
            { buf, kBufSize },
        },
    );

    if (R_FAILED(rc)) return rc;

    out.key  = key;
    out.type = type;

    IpcReader rd(buf, kBufSize);
    switch (type) {
        case KdecSettingType::Bool:   out.value = rd.read<bool>();    break;
        case KdecSettingType::Int:    out.value = rd.read<int32_t>(); break;
        case KdecSettingType::String: out.value = rd.read_string();   break;
    }

    return rd.ok() ? 0 : MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

Result kdecIpcWriteSetting(const KdecSettingEntry& entry) {
    IpcWriter wr;
    switch (entry.type) {
        case KdecSettingType::Bool:   wr.write(std::get<bool>(entry.value));        break;
        case KdecSettingType::Int:    wr.write(std::get<int32_t>(entry.value));     break;
        case KdecSettingType::String: wr.write_string(std::get<std::string>(entry.value)); break;
    }

    KdecWireSettingType wire{ static_cast<uint32_t>(entry.type) };

    return serviceDispatchIn(&g_kdecSrv, KdecIpcCmd_WriteSetting, wire,
        .buffer_attrs = {
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
        },
        .buffers = {
            { entry.key.c_str(), entry.key.size() + 1 },
            { wr.data().data(), wr.size() },
        },
    );
}

Result kdecIpcGetAllSettings(std::vector<KdecSettingEntry>& out) {
    static constexpr size_t kBufSize = 64 * 1024;
    uint8_t buf[kBufSize];

    uint32_t count = 0;
    Result rc = serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetAllSettings, count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ buf, kBufSize }},
    );

    if (R_FAILED(rc)) return rc;

    out.clear();
    IpcReader rd(buf, kBufSize);
    for (uint32_t i = 0; i < count; i++) {
        if (!rd.ok()) break;
        KdecSettingEntry entry;
        entry.key  = rd.read_string();
        entry.type = static_cast<KdecSettingType>(rd.read<uint8_t>());
        switch (entry.type) {
            case KdecSettingType::Bool:   entry.value = rd.read<bool>();    break;
            case KdecSettingType::Int:    entry.value = rd.read<int32_t>(); break;
            case KdecSettingType::String: entry.value = rd.read_string();   break;
        }
        if (rd.ok()) out.push_back(std::move(entry));
    }
    return 0;
}
