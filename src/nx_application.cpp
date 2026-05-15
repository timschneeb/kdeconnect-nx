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

NxApplication::NxApplication() : was_online_(false), storage_(Storage()) {
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

void NxApplication::processEvents() {
    if (client_->needs_restart()) {
        Logger::info("Client requested restart, restarting...");
        client_ = std::make_shared<KdeConnectClient>(storage_);
        if (!client_->start()) {
            Logger::error("Failed to restart client after sleep.");
            // TODO: add sleep later after conversion to sysmodule
            // svcSleepThread(3'000'000'000LL);
        }
    }

    const bool now_online = isOnline();
    if (was_online_ && !now_online) {
        Logger::info("Network connection lost.");
    } else if (!was_online_ && now_online) {
        Logger::info("Network restored, restarting client...");
        client_ = std::make_shared<KdeConnectClient>(storage_);
        if (!client_->start()) {
            Logger::error("Failed to restart client after network restore.");
            // TODO: add sleep later after conversion to sysmodule
            //svcSleepThread(3'000'000'000LL);
        }
    }
    if (!now_online) {
        // TODO: add sleep later after conversion to sysmodule
        //svcSleepThread(2'000'000'000LL);
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
