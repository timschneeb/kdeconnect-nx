#pragma once
#include "storage.h"
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

    bool was_online_;
    Storage storage_;
    std::shared_ptr<KdeConnectClient> client_;
};
