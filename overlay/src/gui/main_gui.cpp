#include "main_gui.h"
#include "device_gui.h"
#include "settings_gui.h"
#include "gui_common.h"

#include <kdec/ipc.h>
#include <kdec/ipc_client.h>

#include <cstdio>
#include <vector>

#include "error_widget.h"
#include "logger.h"
#include "../utils/symbols.h"
#include "elements/large_icon_list_item.h"

namespace {

constexpr auto kUltrahandNotifFlag =
    "sdmc:/config/ultrahand/flags/NOTIFICATIONS.flag";

bool ultrahandNotificationsEnabled() {
    FILE* f = fopen(kUltrahandNotifFlag, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

bool userWantsNotifications() {
    bool show_remote = true, show_connect = false;
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowRemoteMessages, show_remote);
    kdecIpcReadBoolSetting(KdecBoolSettingKey::NotificationShowOnConnect, show_connect);
    return show_remote || show_connect;
}
} // namespace

bool MainGui::devicesChanged(const std::vector<KdecDeviceInfo> &a, const std::vector<KdecDeviceInfo> &b) {
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (strcmp(a[i].id, b[i].id) != 0) return true;
        if (strcmp(a[i].name, b[i].name) != 0) return true;
        if (a[i].pair_state != b[i].pair_state) return true;
        if (a[i].is_connected != b[i].is_connected) return true;
        if (a[i].battery_level != b[i].battery_level) return true;
        if (a[i].is_charging != b[i].is_charging) return true;
    }
    return false;
}

tsl::elm::Element *MainGui::createUI() {
    const bool running = kdecIpcRunning();
    was_running_ = running;

    const char *subtitle = running ? "..." : "Sysmodule not running";
    uint32_t ver = 0;
    if (running) {
        if (R_SUCCEEDED(kdecIpcGetApiVersion(ver)))
            snprintf(subtitle_buf_, sizeof(subtitle_buf_), "Version %s \xc2\xb7 API v%u", MINIKDECONNECT_VERSION, ver);
        subtitle = subtitle_buf_;
    }

    auto *frame = new tsl::elm::OverlayFrame("KDE Connect NX", subtitle);
    tsl::elm::List *list = nullptr;

    auto onClickRestartSysModule = [](const u64 keys) -> bool {
        if (keys & HidNpadButton_A) {
            if (R_SUCCEEDED(pmshellInitialize())) {
                pmshellTerminateProgram(SYSMODULE_TITLE_ID);
                svcSleepThread(100'000'000LL);

                constexpr NcmProgramLocation programLocation {
                    .program_id = SYSMODULE_TITLE_ID,
                    .storageID = NcmStorageId_None,
                };
                u64 pid = 0;
                pmshellLaunchProgram(0, &programLocation, &pid);
                pmshellExit();
                svcSleepThread(100'000'000LL);
            }
            return true;
        }
        return false;
    };

    if (!running) {
        list = ErrorWidget::create(sym::errorCircleFilled, "Sysmodule not running", "Restart sysmodule",
                                   onClickRestartSysModule);
    } else {
        if (ver > 0 && ver != KDEC_IPC_API_VERSION) {
            list = ErrorWidget::create(sym::errorCircleFilled,
                                       "IPC version mismatch\n\n"
                                       "Overlay and sysmodule\n"
                                       "are on different versions.\n"
                                       "Overlay: v" + std::to_string(KDEC_IPC_API_VERSION) + "; Sysmodule: v" + std::to_string(ver),
                                       "Restart sysmodule",
                                       onClickRestartSysModule
            );
        }

        if (!devices_prefetched_) {
            Result rc = kdecIpcGetDevices(devices_);
            if (R_FAILED(rc)) {
                char buf[48];
                snprintf(buf, sizeof(buf), "IPC error: %d-%d", R_MODULE(rc), R_DESCRIPTION(rc));
                list = ErrorWidget::create(sym::errorCircleFilled, buf);
                frame->setContent(list);
                return frame;
            }
            devices_prefetched_ = true;
        }

        list = new tsl::elm::List();

        if (!ultrahandNotificationsEnabled() && userWantsNotifications()) {
            auto* warn = new LargeIconListItem(
                "Notification support in",
                "Ultrahand is disabled!",
                "Check your Ultrahand settings", sym::infoCircleFilled);
            list->addItem(warn);
        }

        if (devices_.empty()) {
            list->addItem(new tsl::elm::ListItem("No devices found"));

            const auto lineHeight = tsl::gfx::FontManager::getFontMetricsForCharacter('A', 16).lineHeight + 5;
            constexpr auto topMargin = 40;
            auto* help_item = new tsl::elm::CustomDrawer([lineHeight](tsl::gfx::Renderer* renderer, const s32 x, const s32 y, s32 w, s32 h) {
                renderer->drawString("The app is scanning continuously for devices", false, x + 5, y + topMargin, 16, DESC_COLOR);
                renderer->drawString("and will update this list automatically.", false, x + 5, y + lineHeight + topMargin, 16, DESC_COLOR);
                renderer->drawString("- Make sure you are in the same local network.", false, x + 5, y + lineHeight*3 + topMargin, 16, DESC_COLOR);
                renderer->drawString("- Make sure your router has client isolation off.", false, x + 5, y + lineHeight*4 + topMargin, 16, DESC_COLOR);

            });
            help_item->setBoundaries(help_item->getX(), help_item->getY(), help_item->getWidth(), lineHeight*4 + topMargin);
            list->addItem(help_item);
        } else {
            std::vector<const KdecDeviceInfo *> connected, unpaired, disconnected;
            for (const auto &dev: devices_) {
                if (dev.pair_state == DevicePairState::Paired)
                    (dev.is_connected ? connected : disconnected).push_back(&dev);
                else
                    unpaired.push_back(&dev);
            }

            auto addDevItem = [&](const KdecDeviceInfo &dev) {
                auto *item = new tsl::elm::ListItem(devName(dev), batteryOrStatusStr(dev));
                item->setClickListener([dev](const u64 keys) -> bool {
                    if (keys & HidNpadButton_A) {
                        tsl::changeTo<DeviceGui>(dev);
                        return true;
                    }
                    return false;
                });
                list->addItem(item);
            };

            if (!connected.empty()) {
                list->addItem(new tsl::elm::CategoryHeader("Connected"));
                for (auto *dev: connected) addDevItem(*dev);
            }
            if (!unpaired.empty()) {
                list->addItem(new tsl::elm::CategoryHeader("Unpaired"));
                for (auto *dev: unpaired) addDevItem(*dev);
            }
            if (!disconnected.empty()) {
                list->addItem(new tsl::elm::CategoryHeader("Disconnected"));
                for (auto *dev: disconnected) addDevItem(*dev);
            }

            if (!focused_text_.empty())
                list->jumpToItem(focused_text_);
        }

        list->addItem(new tsl::elm::CategoryHeader("Options"));
        auto *settings_item = new tsl::elm::ListItem(std::string(sym::settings) + " Settings", sym::chevronRight);
        settings_item->setClickListener([](const u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<SettingsGui>();
                return true;
            }
            return false;
        });
        list->addItem(settings_item);
    }

    frame->setContent(list);
    return frame;
}

bool MainGui::handleInput(const u64 keysDown, u64 keysHeld, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        if (auto* ovl = tsl::Overlay::get())
            ovl->close();
        return true;
    }
    return false;
}

void MainGui::update() {
    if (++tick_ < 120) return;
    tick_ = 0;

    const bool now_running = kdecIpcRunning();
    Result rc;

    // Lazily connect when the service comes up (initServices skips if not running).
    if (now_running && !kdecIpcIsConnected()) {
        if (rc = kdecIpcInitialize(); R_FAILED(rc)) {
            Logger::error("Failed to connect to KDE Connect service: %d-%d", R_MODULE(rc), R_DESCRIPTION(rc));
        }
    }

    // Release handle when the service goes away so we reconnect cleanly next time.
    if (!now_running && kdecIpcIsConnected()) {
        kdecIpcExit();
    }

    std::vector<KdecDeviceInfo> fresh;
    if (now_running) {
        rc = kdecIpcGetDevices(fresh);
        if (R_FAILED(rc)) {
            Logger::error("Failed to call to kdecIpcGetDevices: %d-%d", R_MODULE(rc), R_DESCRIPTION(rc));
            return;
        }
    }

    // TODO: this is unreliable for some reason now
    if (now_running != was_running_ || devicesChanged(devices_, fresh))
        tsl::changeTo<MainGui>(s_lastFocusedItemText, std::move(fresh));
}
