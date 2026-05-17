#pragma once

#include <switch.h>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "../nx_application.h"
#include "ipc_server.h"
#include <../../../common/src/kdec/ipc.h>

class IpcService {
public:
    IpcService(NxApplication* app);
    ~IpcService();

    void start();
    void stop();

private:
    static void thread_func(void* arg);
    static Result handle_command_static(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_size);
    Result handle_command(u32 cmd_id, const IpcServerRequest* r, u8* out_data, size_t* out_size);

    NxApplication* app_;
    std::atomic<bool> running_;
    Thread thread_{};
    IpcServer srv_{};

    mutable std::mutex settings_mutex_;
    std::unordered_map<std::string, KdecSettingEntry> settings_;
};
