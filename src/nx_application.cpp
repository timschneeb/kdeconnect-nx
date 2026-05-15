#include "nx_application.h"

#include <switch.h>

#include <algorithm>
#include <string>

#include "plugins/share_plugin.h"
#include "kdeconnect_client.h"
#include "storage.h"
#include "utils/logger.h"
#include "utils/mem_debug.h"

#define EXIT_WITH_ERROR(fmt, ...) { \
        Logger::error(fmt __VA_OPT__(,) __VA_ARGS__); \
        printf(fmt"\n" __VA_OPT__(,) __VA_ARGS__); \
        consoleUpdate(NULL); \
        svcSleepThread(5'000'000'000LL); \
        consoleExit(NULL); \
        exit(1); \
    }

NxApplication::NxApplication() : was_online_(false), storage_(Storage()),
                                last_restart_(std::chrono::steady_clock::now()),
                                restart_cooldown_(std::chrono::seconds(3)) {
    consoleInit(NULL);
    Logger::connect_nxlink();

    if (Result rc = socketInitializeDefault(); R_FAILED(rc))
        EXIT_WITH_ERROR("socketInitializeDefault failed: 0x%x", rc);

    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
        EXIT_WITH_ERROR("nifmInitialize failed");

    was_online_ = isOnline();

    client_ = std::make_shared<KdeConnectClient>(storage_);
    if (!client_->start())
        EXIT_WITH_ERROR("Failed to start KDE Connect client");
}

NxApplication::~NxApplication() {
    client_.reset();
    Logger::shutdown();
    nifmExit();
    socketExit();
    consoleExit(NULL);
}

void NxApplication::restart_client(const char* reason) {
    if (std::chrono::steady_clock::now() - last_restart_ < restart_cooldown_) {
        return;
    }
    Logger::info(reason);
    last_restart_ = std::chrono::steady_clock::now();
    client_ = std::make_shared<KdeConnectClient>(storage_);
    if (!client_->start()) {
        Logger::error("Failed to restart client.");
    }
}

void NxApplication::processEvents() {
    if (client_->needs_restart()) {
        restart_client("Client requested restart, restarting...");
    }

    const bool now_online = isOnline();
    if (was_online_ && !now_online) {
        Logger::info("Network connection lost.");
    } else if (!was_online_ && now_online) {
        restart_client("Network restored, restarting client...");
    }
    was_online_ = now_online;

    // Open any URL shared from the desktop (blocks while browser is open).
    SharePlugin::open_pending_url();
}

bool NxApplication::isOnline() {
    NifmInternetConnectionType type;
    std::uint32_t wifi;
    NifmInternetConnectionStatus status;
    nifmGetInternetConnectionStatus(&type, &wifi, &status);
    return status == NifmInternetConnectionStatus_Connected;
}
