#pragma once
#include "utils/storage.h"
#include "utils/psc_monitor.h"
#include <chrono>
#include <memory>

class KdeConnectClient;
class IpcService;

class NxApplication {
public:
    NxApplication();
    ~NxApplication();
    void processEvents();
    std::shared_ptr<KdeConnectClient> client() const { return client_; }

private:
    static bool isOnline();
    void restart_client(const char* reason);

    bool was_online_;
    bool has_initialized_nxlink_;
    Storage storage_;
    PscMonitor psc_monitor_;
    std::shared_ptr<KdeConnectClient> client_;
    std::unique_ptr<IpcService> ipc_service_;
    std::chrono::steady_clock::time_point last_restart_;
    std::chrono::milliseconds restart_cooldown_;
};
