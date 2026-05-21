#include "main_gui.h"
#include "device_gui.h"
#include "gui_common.h"
#include <kdec/ipc.h>
#include <kdec/ipc_client.h>
#include <cstdio>
#include <vector>

bool MainGui::devicesChanged(const std::vector<KdecDeviceInfo>& a, const std::vector<KdecDeviceInfo>& b) {
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (strcmp(a[i].id,   b[i].id)   != 0) return true;
        if (strcmp(a[i].name, b[i].name) != 0) return true;
        if (a[i].pair_state   != b[i].pair_state)   return true;
        if (a[i].is_connected != b[i].is_connected) return true;
        if (a[i].battery_level != b[i].battery_level) return true;
        if (a[i].is_charging != b[i].is_charging) return true;
    }
    return false;
}

tsl::elm::Element* MainGui::createUI() {
    const bool running = kdecIpcRunning();
    was_running_ = running;

    const char* subtitle = running ? "KDE Connect NX" : "Sysmodule not running";
    if (running) {
        uint32_t ver = 0;
        if (R_SUCCEEDED(kdecIpcGetApiVersion(ver)))
            snprintf(subtitle_buf_, sizeof(subtitle_buf_), "Version %s \xc2\xb7 API v%u", MINIKDECONNECT_VERSION, ver);
        subtitle = subtitle_buf_;
    }

    auto* frame = new tsl::elm::OverlayFrame("KDE Connect NX", subtitle);
    auto* list  = new tsl::elm::List();

    if (!running) {
        list->addItem(new tsl::elm::ListItem("Sysmodule is not active"));
    } else {
        if (!devices_prefetched_) {
            Result rc = kdecIpcGetDevices(devices_);
            if (R_FAILED(rc)) {
                char buf[48];
                snprintf(buf, sizeof(buf), "IPC error: 0x%08X", rc);
                list->addItem(new tsl::elm::ListItem(buf));
                frame->setContent(list);
                return frame;
            }
        }

        if (devices_.empty()) {
            list->addItem(new tsl::elm::ListItem("No devices found"));
        } else {
            std::vector<const KdecDeviceInfo*> connected, unpaired, disconnected;
            for (const auto& dev : devices_) {
                if (dev.pair_state == DevicePairState::Paired)
                    (dev.is_connected ? connected : disconnected).push_back(&dev);
                else
                    unpaired.push_back(&dev);
            }

            auto addDevItem = [&](const KdecDeviceInfo& dev) {
                auto* item = new tsl::elm::ListItem(devName(dev), batteryOrStatusStr(dev));
                item->setClickListener([dev](u64 keys) -> bool {
                    if (keys & HidNpadButton_A) { tsl::changeTo<DeviceGui>(dev); return true; }
                    return false;
                });
                list->addItem(item);
            };

            if (!connected.empty()) {
                list->addItem(new tsl::elm::CategoryHeader("Connected"));
                for (auto* dev : connected) addDevItem(*dev);
            }
            if (!unpaired.empty()) {
                list->addItem(new tsl::elm::CategoryHeader("Unpaired"));
                for (auto* dev : unpaired) addDevItem(*dev);
            }
            if (!disconnected.empty()) {
                list->addItem(new tsl::elm::CategoryHeader("Disconnected"));
                for (auto* dev : disconnected) addDevItem(*dev);
            }

            if (!focused_text_.empty())
                list->jumpToItem(focused_text_);
        }
    }

    frame->setContent(list);
    return frame;
}

bool MainGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::Overlay::get()->close();
        return true;
    }
    return false;
}

void MainGui::update() {
    if (++tick_ < 120) return;
    tick_ = 0;

    const bool now_running = kdecIpcRunning();
    std::vector<KdecDeviceInfo> fresh;

    if (now_running && R_FAILED(kdecIpcGetDevices(fresh))) return;

    if (now_running != was_running_ || devicesChanged(devices_, fresh))
        tsl::changeTo<MainGui>(s_lastFocusedItemText, std::move(fresh));
}