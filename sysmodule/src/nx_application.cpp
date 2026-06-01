#include "src/nx_application.h"

#include <switch.h>

#include <algorithm>
#include <malloc.h>
#include <string>

#include "net/kdeconnect_client.h"
#include "plugins/share_plugin.h"
#include "utils/storage.h"
#include "utils/logger.h"
#include "utils/mem_debug.h"
#include "ipc/ipc_service.h"

constexpr bool kEnableMdns = true;

NxApplication::NxApplication() : was_online_(false), storage_(Storage()),
                                 last_restart_(std::chrono::steady_clock::now()),
                                 restart_cooldown_(std::chrono::seconds(3)) {
#if defined(NXLINK_ENABLED)
    Logger::set_nxlink_host(NXLINK_HOST, NxLink::kDefaultPort);
#endif

    psc_monitor_.start();

    ipc_service_ = std::make_unique<IpcService>(this);
    ipc_service_->start();

    was_online_ = isOnline();
    if (was_online_) {
        has_initialized_nxlink_ = true;
        Logger::connect_nxlink();
    }

    client_ = std::make_shared<KdeConnectClient>(storage_);

    if (!client_->start(kEnableMdns))
        Logger::error("Failed to start KDE Connect client");
}

NxApplication::~NxApplication() {
    psc_monitor_.stop();
    ipc_service_->stop();
    client_.reset();
    Logger::shutdown();
}

void NxApplication::restart_client(const char* reason, bool force) {
    if (!force && std::chrono::steady_clock::now() - last_restart_ < restart_cooldown_) {
        return;
    }
    Logger::info("%s", reason);
    last_restart_ = std::chrono::steady_clock::now();
    client_.reset();     // destroy old client before allocating the new one
    malloc_trim(0);      // return top-of-arena free chunk to the sbrk pool
    client_ = std::make_shared<KdeConnectClient>(storage_);
    if (!client_->start(kEnableMdns)) {
        Logger::error("Failed to restart client.");
    }
}

void NxApplication::processEvents() {
    if (client_->needs_restart()) {
        restart_client("Client requested restart, restarting...", false);
    }

    const bool now_online = isOnline();
    if (was_online_ && !now_online) {
        Logger::info("Network connection lost.");
    } else if (!was_online_ && now_online) {
        // Allow late-init once the network is ready
        if (!has_initialized_nxlink_)
            Logger::connect_nxlink();
        restart_client("Network restored, restarting client...", true);
    }
    was_online_ = now_online;

    for (const auto& [id, session] : client_->devices()) {
        if (session->disconnected.load()) continue;
        for (const auto& plugin : session->plugins) {
            plugin->process_events();
        }
    }
}

bool NxApplication::isOnline() {
    NifmInternetConnectionType type;
    std::uint32_t wifi;
    NifmInternetConnectionStatus status;
    nifmGetInternetConnectionStatus(&type, &wifi, &status);
    return status == NifmInternetConnectionStatus_Connected;
}
