#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <kdec/ipc_client.h>
#include <src/utils/logger.h>

#include "gui/main_gui.h"

class OverlayMain : public tsl::Overlay {
public:
    void initServices() override {
#ifdef NXLINK_ENABLED
        constexpr SocketInitConfig socketInitConfig = {
            .tcp_tx_buf_size     = 16 * 1024,
            .tcp_rx_buf_size     = 16 * 1024,
            .tcp_tx_buf_max_size = 32 * 1024,
            .tcp_rx_buf_max_size = 32 * 1024,
            .udp_tx_buf_size     = 0,
            .udp_rx_buf_size     = 0,
            .sb_efficiency       = 1,
            .bsd_service_type    = BsdServiceType_Auto
        };
        socketInitialize(&socketInitConfig);
        ASSERT_FATAL(timeInitialize());
#endif
        ASSERT_FATAL(smInitialize());

        Logger::open_log_file("kdeconnect_overlay");
#if defined(NXLINK_ENABLED)
        Logger::set_nxlink_host(NXLINK_HOST, NxLink::kDefaultPort + 1);
        Logger::connect_nxlink();
#endif
        // Only connect if the sysmodule is already running. smGetService blocks
        // indefinitely if the service hasn't registered yet, which would freeze
        // the overlay before any GUI is shown. The GUI retries lazily via update().
        if (kdecIpcRunning()) {
            kdecIpcInitialize();
        } else {
            Logger::warn("Sysmodule not running at startup; will connect when available");
        }
    }

    void exitServices() override {
        kdecIpcExit();
        Logger::shutdown();
#ifdef NXLINK_ENABLED
        socketExit();
        timeExit();
#endif
        smExit();
    }

    void onShow() override {
        if (kdecIpcIsConnected())
            kdecIpcSendBroadcast();
    }
    void onHide() override {}

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

int main(int argc, char** argv) {
    return tsl::loop<OverlayMain>(argc, argv);
}
