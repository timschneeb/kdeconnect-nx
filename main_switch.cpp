#include <switch.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

//#define MEM_DEBUG

#include "src/kdeconnect_client.h"
#include "src/storage.h"
#include "src/utils/logger.h"
#include "src/utils/mem_debug.h"
#include "src/plugins/ping_plugin.h"
#include "src/plugins/battery_plugin.h"
#include "src/plugins/find_my_phone_plugin.h"
#include "src/plugins/mpris_plugin.h"
#include "src/plugins/share_plugin.h"
#include "src/plugins/mousepad_plugin.h"

// ---------------------------------------------------------------------------
// Log ring buffer (written from any thread, read by the main/UI thread)
// ---------------------------------------------------------------------------

static constexpr size_t kMaxLogLines = 10;
static std::deque<std::string> s_log_buf;
static std::mutex s_log_mutex;

static void log_sink(const std::string& level, const std::string& msg) {
    std::lock_guard lock(s_log_mutex);
    s_log_buf.push_back("[" + level + "] " + msg);
    if (s_log_buf.size() > kMaxLogLines) {
        s_log_buf.pop_front();
    }
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

static void draw_ui(KdeConnectClient& client, int selected) {
    consoleClear();

    printf("MiniKDEConnect for Nintendo Switch\n");
    printf("===================================\n");
    printf("Device ID: %.36s\n\n", client.local_device().id.c_str());

    auto devices_map = client.devices();
    std::vector<std::pair<std::string, std::shared_ptr<KdeConnectClient::DeviceSession>>> devices(
        devices_map.begin(), devices_map.end());

    printf("Devices (%zu):\n", devices.size());
    if (devices.empty()) {
        printf("  (waiting for connections...)\n");
    }
    for (int i = 0; i < static_cast<int>(devices.size()); ++i) {
        const auto& [id, sess] = devices[i];
        const char* marker = (i == selected) ? ">" : " ";

        const char* state;
        if (sess->disconnected.load())                             state = "disconnected";
        else if (sess->pair_state == PairState::RequestedByPeer)  state = "PAIR REQUEST!";
        else if (sess->paired)                                     state = "paired";
        else                                                       state = "unpaired";

        printf(" %s %-20s (%8.8s) [%s]\n",
            marker,
            sess->info.name.empty() ? "Unknown" : sess->info.name.c_str(),
            id.c_str(),
            state);
    }

    // Show MPRIS info for selected paired device
    if (!devices.empty() && selected < static_cast<int>(devices.size())) {
        const auto& sess = devices[selected].second;
        if (sess->paired && !sess->disconnected.load()) {
            if (auto* mpris = sess->plugin<MprisPlugin>()) {
                const std::string player = mpris->current_player();
                if (!player.empty()) {
                    const auto ps = mpris->player_state();
                    printf("\n  Media [%s]: %s%s%s\n",
                        player.c_str(),
                        ps.is_playing ? "> " : "|| ",
                        ps.artist.empty() ? "" : (ps.artist + " - ").c_str(),
                        ps.title.c_str());
                }
            }
        }
    }

    printf("\n");
    printf("[+] Exit   [Up/Down] Navigate\n");
    printf("[A] Pair/Accept  [B] Unpair/Reject  [X] Ping  [Y] Find\n");
    printf("[ZL] Send battery  [ZR] MPRIS list  [L] Prev  [R] Next\n");
    printf("[-] Type text (keyboard test)\n");

#ifdef MEM_DEBUG
    {
        const auto mem = get_mem_stats();
        const auto al  = get_alloc_stats();
        printf("\nMemory:\n");
        printf("  Proc : %3llu / %3llu MB\n",
            static_cast<unsigned long long>(mem.proc_used_mb),
            static_cast<unsigned long long>(mem.proc_total_mb));
        printf("  Heap : %5zu KB used / %5zu KB total  (peak %5zu KB)\n",
            mem.heap_used_kb, mem.heap_total_kb, mem.heap_peak_kb);
        printf("  new  : %5zu KB live  (peak %5zu KB)  allocs: %zu live / %zu total\n",
            al.live_kb, al.peak_kb, al.live_allocs, al.total_allocs);
    }
#endif

    printf("\nLog:\n");
    {
        std::lock_guard lock(s_log_mutex);
        for (const auto& line : s_log_buf) {
            printf("  %.76s\n", line.c_str());
        }
    }

    consoleUpdate(NULL);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
    consoleInit(NULL);

    Logger::set_sink(log_sink);
    Logger::connect_nxlink();


    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        printf("socketInitializeDefault failed: 0x%x\n", rc);
        consoleUpdate(NULL);
        svcSleepThread(3'000'000'000LL);
        consoleExit(NULL);
        return 1;
    }

    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
        Logger::error("nifmInitialize failed");

    Storage storage;
    auto client = std::make_unique<KdeConnectClient>(storage);

    if (!client->start()) {
        printf("Failed to start KDE Connect client.\n");
        consoleUpdate(NULL);
        svcSleepThread(3'000'000'000LL);
        socketExit();
        consoleExit(NULL);
        return 1;
    }

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    int selected = 0;

    auto is_net_connected = []() -> bool {
        NifmInternetConnectionType type;
        std::uint32_t wifi;
        NifmInternetConnectionStatus status;
        nifmGetInternetConnectionStatus(&type, &wifi, &status);
        return status == NifmInternetConnectionStatus_Connected;
    };
    bool net_was_connected = is_net_connected();

    while (appletMainLoop()) {
        if (client->needs_restart()) {
            Logger::info("Client requested restart, restarting...");
            selected = 0;
            client = std::make_unique<KdeConnectClient>(storage);
            if (!client->start()) {
                Logger::error("Failed to restart client after sleep.");
                // TODO: add sleep later after conversion to sysmodule
                // svcSleepThread(3'000'000'000LL);
            }
        }

        const bool net_now = is_net_connected();
        if (net_was_connected && !net_now) {
            Logger::info("Network connection lost.");
        } else if (!net_was_connected && net_now) {
            Logger::info("Network restored, restarting client...");
            selected = 0;
            client = std::make_unique<KdeConnectClient>(storage);
            if (!client->start()) {
                Logger::error("Failed to restart client after network restore.");
                // TODO: add sleep later after conversion to sysmodule
                //svcSleepThread(3'000'000'000LL);
            }
        }
        if (!net_now) {
            // TODO: add sleep later after conversion to sysmodule
            //svcSleepThread(2'000'000'000LL);
        }
        net_was_connected = net_now;

        padUpdate(&pad);
        const u64 kDown = padGetButtonsDown(&pad);

        // Exit
        if (kDown & HidNpadButton_Plus) break;

        // Build device list snapshot for input handling
        auto devices_map = client->devices();
        std::vector<std::pair<std::string, std::shared_ptr<KdeConnectClient::DeviceSession>>> devices(
            devices_map.begin(), devices_map.end());

        if (!devices.empty()) {
            selected = std::clamp(selected, 0, static_cast<int>(devices.size()) - 1);
        }

        auto get_session = [&]() -> std::shared_ptr<KdeConnectClient::DeviceSession> {
            if (devices.empty() || selected >= static_cast<int>(devices.size())) return nullptr;
            return devices[selected].second;
        };
        auto get_id = [&]() -> std::string {
            if (devices.empty() || selected >= static_cast<int>(devices.size())) return {};
            return devices[selected].first;
        };

        // Navigation
        if ((kDown & HidNpadButton_AnyDown) && !devices.empty())
            selected = (selected + 1) % static_cast<int>(devices.size());
        if ((kDown & HidNpadButton_AnyUp) && !devices.empty())
            selected = (selected - 1 + static_cast<int>(devices.size())) % static_cast<int>(devices.size());

        // Pair / Accept
        if (kDown & HidNpadButton_A) {
            const auto id = get_id();
            if (!id.empty()) {
                auto sess = get_session();
                if (sess->pair_state == PairState::RequestedByPeer)
                    client->accept_pair(id);
                else
                    client->request_pair(id);
            }
        }
        // Reject / Unpair
        if (kDown & HidNpadButton_B) {
            const auto id = get_id();
            if (!id.empty()) {
                auto sess = get_session();
                if (sess->paired) client->unpair(id);
                else              client->reject_pair(id);
            }
        }
        // Ping
        if (kDown & HidNpadButton_X) {
            if (auto sess = get_session()) {
                if (auto* p = sess->plugin<PingPlugin>())
                    p->ping("Hello from Nintendo Switch!");
            }
        }
        // Find
        if (kDown & HidNpadButton_Y) {
            if (auto sess = get_session()) {
                if (auto* p = sess->plugin<FindMyPhonePlugin>())
                    p->find();
            }
        }
        // Send battery status
        if (kDown & HidNpadButton_ZL) {
            if (auto sess = get_session()) {
                if (auto* p = sess->plugin<BatteryPlugin>())
                    p->send_status();
            }
        }
        // Request MPRIS player list
        if (kDown & HidNpadButton_ZR) {
            if (auto sess = get_session()) {
                if (auto* p = sess->plugin<MprisPlugin>())
                    p->request_player_list();
            }
        }
        // MPRIS previous
        if (kDown & HidNpadButton_L) {
            if (auto sess = get_session()) {
                if (auto* p = sess->plugin<MprisPlugin>()) {
                    const std::string player = p->current_player();
                    if (!player.empty()) p->send_action(player, "Previous");
                }
            }
        }
        // MPRIS next
        if (kDown & HidNpadButton_R) {
            if (auto sess = get_session()) {
                if (auto* p = sess->plugin<MprisPlugin>()) {
                    const std::string player = p->current_player();
                    if (!player.empty()) p->send_action(player, "Next");
                }
            }
        }

        // Keyboard test: open swkbd
        if (kDown & HidNpadButton_Minus) {
            if (auto sess = get_session(); sess && sess->paired) {
                if (auto* mp = sess->plugin<MousepadPlugin>()) {
                    SwkbdConfig kbd;
                    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
                        swkbdConfigSetGuideText(&kbd, "Example input");
                        swkbdConfigSetStringLenMax(&kbd, 256);
                        char buf[257] = {};
                        if (R_SUCCEEDED(swkbdShow(&kbd, buf, sizeof(buf))) && buf[0] != '\0') {}
                        swkbdClose(&kbd);
                    }
                }
            }
        }

        // Open any URL shared from the desktop (blocks while browser is open).
        SharePlugin::open_pending_url();

        draw_ui(*client, selected);
        svcSleepThread(100'000'000LL); // 100 ms
    }

    client.reset();
    Logger::shutdown();
    nifmExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
