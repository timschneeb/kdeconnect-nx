#include <switch.h>
#include <atomic>
#include <cstring>

#include "ipc.h"

static Service g_kdecSrv;
static std::atomic<size_t> g_refCnt;

extern "C" {

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

Result kdecIpcGetApiVersion(uint32_t* out_ver) {
    return serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetApiVersion, *out_ver);
}

Result kdecIpcGetDeviceCount(uint32_t* out_count) {
    return serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetDeviceCount, *out_count);
}

Result kdecIpcGetDevices(KdecDeviceInfo* out_devices, uint32_t max_count, uint32_t* out_count) {
    return serviceDispatchOut(&g_kdecSrv, KdecIpcCmd_GetDevices, *out_count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = {{ out_devices, max_count * sizeof(KdecDeviceInfo) }},
    );
}

Result kdecIpcRequestPair(const char* device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_RequestPair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id, strlen(device_id) + 1 }},
    );
}

Result kdecIpcAcceptPair(const char* device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_AcceptPair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id, strlen(device_id) + 1 }},
    );
}

Result kdecIpcRejectPair(const char* device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_RejectPair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id, strlen(device_id) + 1 }},
    );
}

Result kdecIpcUnpair(const char* device_id) {
    return serviceDispatch(&g_kdecSrv, KdecIpcCmd_Unpair,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = {{ device_id, strlen(device_id) + 1 }},
    );
}

}
