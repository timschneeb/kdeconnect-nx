#pragma once
#include "utils/storage.h"
#include <chrono>
#include <memory>

class KdeConnectClient;

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
    Storage storage_;
    std::shared_ptr<KdeConnectClient> client_;
    std::chrono::steady_clock::time_point last_restart_;
    std::chrono::milliseconds restart_cooldown_;
};
