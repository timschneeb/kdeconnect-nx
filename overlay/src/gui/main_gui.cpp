#include "main_gui.h"
#include "device_gui.h"
#include "gui_common.h"
#include <kdec/ipc.h>
#include <kdec/ipc_client.h>
#include <cstdio>
#include <vector>

tsl::elm::Element* MainGui::createUI() {
    bool running = kdecIpcRunning();

    const char* subtitle = running ? "KDE Connect NX" : "Sysmodule not running";
    if (running) {
        uint32_t ver = 0;
        if (R_SUCCEEDED(kdecIpcGetApiVersion(ver)))
            snprintf(subtitle_buf_, sizeof(subtitle_buf_), "KDE Connect NX \xc2\xb7 API v%u", ver);
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
            for (const auto& dev : devices) {
                auto* item = new tsl::elm::ListItem(devName(dev), statusStr(dev));
                item->setClickListener([dev](u64 keys) -> bool {
                    if (keys & HidNpadButton_A) { tsl::changeTo<DeviceGui>(dev); return true; }
                    return false;
                });
                list->addItem(item);
            }
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
    if (++tick_ >= 120) {
        tick_ = 0;
        tsl::changeTo<MainGui>();
    }
}
