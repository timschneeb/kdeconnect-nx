#pragma once

#include <switch.h>
#include <kdec/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

bool kdecIpcRunning();
Result kdecIpcInitialize();
void kdecIpcExit();

Result kdecIpcGetApiVersion(uint32_t* out_ver);
Result kdecIpcGetDeviceCount(uint32_t* out_count);
Result kdecIpcGetDevices(KdecDeviceInfo* out_devices, uint32_t max_count, uint32_t* out_count);

Result kdecIpcRequestPair(const char* device_id);
Result kdecIpcAcceptPair(const char* device_id);
Result kdecIpcRejectPair(const char* device_id);
Result kdecIpcUnpair(const char* device_id);

#ifdef __cplusplus
}
#endif
