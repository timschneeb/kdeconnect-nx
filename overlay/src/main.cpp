#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <kdec/ipc.h>
#include <kdec/ipc_client.h>
#include <src/utils/logger.h>
#include <vector>

static const char* pairStateLabel(const KdecDeviceInfo& dev) {
    switch (dev.pair_state) {
        case DevicePairState::Paired:          return dev.is_connected ? "Connected" : "Paired, offline";
        case DevicePairState::RequestedByMe:   return "Pairing…";
        case DevicePairState::RequestedByPeer: return "Pair request";
        default:                               return "Unpaired";
    }
}

class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        bool running = kdecIpcRunning();

        const char* subtitle = running ? "KDE Connect NX" : "Sysmodule not running";
        if (running) {
            uint32_t ver = 0;
            if (R_SUCCEEDED(kdecIpcGetApiVersion(ver)))
                snprintf(subtitle_buf_, sizeof(subtitle_buf_), "KDE Connect NX · API v%u", ver);
            subtitle = subtitle_buf_;
        }

        auto* frame = new tsl::elm::OverlayFrame("KDE Connect NX", subtitle);
        auto* list  = new tsl::elm::List();

        if (!running) {
            list->addItem(new tsl::elm::ListItem("Sysmodule is not active"));
        } else {
            std::vector<KdecDeviceInfo> devices;
            Result rc = kdecIpcGetDevices(devices);

            if (R_FAILED(rc)) {
                char buf[48];
                snprintf(buf, sizeof(buf), "IPC error: 0x%08X", rc);
                list->addItem(new tsl::elm::ListItem(buf));
            } else if (devices.empty()) {
                list->addItem(new tsl::elm::ListItem("No devices found"));
            } else {
                for (const auto& dev : devices)
                    list->addItem(new tsl::elm::ListItem(dev.name, pairStateLabel(dev)));
            }
        }

        frame->setContent(list);
        return frame;
    }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (keysDown & HidNpadButton_B) {
            tsl::Overlay::get()->close();
            return true;
        }
        return false;
    }

    void update() override {
        if (++tick_ >= 120) {
            tick_ = 0;
            tsl::changeTo<MainGui>();
        }
    }

private:
    uint32_t tick_ = 0;
    char subtitle_buf_[64] = {};
};

class OverlayMain : public tsl::Overlay {
public:
    void initServices() override {
#ifdef NXLINK_ENABLED
        constexpr SocketInitConfig socketInitConfig = {
            // TCP buffers
            .tcp_tx_buf_size     = 16 * 1024,   // 16 KB default
            .tcp_rx_buf_size     = 16 * 1024,   // 16 KB default
            .tcp_tx_buf_max_size = 32 * 1024,   // 32 KB max
            .tcp_rx_buf_max_size = 32 * 1024,   // 32 KB max

            // Disable UDP buffers
            .udp_tx_buf_size = 0,
            .udp_rx_buf_size = 0,

            .sb_efficiency       = 1, // Only one buffer
            .bsd_service_type    = BsdServiceType_Auto // Auto-select service
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
        Logger::info("kdecIpcInitialize");
        kdecIpcInitialize();

        Logger::info("kdecIpcInitialize OK");
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
    void onShow() override {}
    void onHide() override {}

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

int main(int argc, char** argv) {
    return tsl::loop<OverlayMain>(argc, argv);
}
