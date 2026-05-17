#pragma once

#include <switch.h>
#include <string>
#include <vector>
#include <kdec/ipc.h>

bool   kdecIpcRunning();
Result kdecIpcInitialize();
void   kdecIpcExit();

Result kdecIpcGetApiVersion(uint32_t& out);
Result kdecIpcGetDeviceCount(uint32_t& out);
Result kdecIpcGetDevices(std::vector<KdecDeviceInfo>& out);

Result kdecIpcRequestPair(const std::string& device_id);
Result kdecIpcAcceptPair(const std::string& device_id);
Result kdecIpcRejectPair(const std::string& device_id);
Result kdecIpcUnpair(const std::string& device_id);

Result kdecIpcPing(const std::string& device_id);
Result kdecIpcGetMediaInfo(KdecMediaInfo& out);
Result kdecIpcSendMediaAction(KdecMediaAction action, int64_t value = 0);
Result kdecIpcGetCommandList(const std::string& device_id, std::vector<KdecCommandEntry>& out);
Result kdecIpcRunCommand(const std::string& device_id, const std::string& command_id);
Result kdecIpcReadSetting(const std::string& key, KdecSettingType type, KdecSettingEntry& out);
Result kdecIpcWriteSetting(const KdecSettingEntry& entry);
Result kdecIpcGetAllSettings(std::vector<KdecSettingEntry>& out);
