#include "volume_gui.h"
#include "gui_common.h"
#include <kdec/ipc_client.h>
#include <cstdio>
#include <string>
#include <vector>

#include "../utils/symbols.h"

VolumeGui::VolumeGui(std::string device_id, std::string device_name)
    : device_id_(std::move(device_id)), device_name_(std::move(device_name)) {}

tsl::elm::Element* VolumeGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Volume", device_name_);

    std::vector<KdecVolumeSinkInfo> sinks;
    Result rc = kdecIpcGetVolumeSinks(device_id_, sinks);

    if (R_FAILED(rc)) {
        char info[48];
        snprintf(info, sizeof(info), "[0x%08X] %04d-%04d", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        std::string info_s(info);
        frame->setContent(new tsl::elm::CustomDrawer([info_s](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString(sym::errorCircleFilled, false, x + 40,  y + 50, 40, TEXT_COLOR);
            renderer->drawString("IPC Error",    false, x + 105, y + 50, 28, TEXT_COLOR);
            renderer->drawString("Could not fetch sinks\nfrom the sysmodule.", false, x + 40, y + 100, 20, TEXT_COLOR);
            renderer->drawString(info_s.c_str(), false, x + 40,  y + 148, 16, DESC_COLOR);
        }));
        return frame;
    }

    if (sinks.empty()) {
        auto* list = new tsl::elm::List();
        auto* empty = new tsl::elm::SilentListItem("No sinks available");
        empty->m_isItem = false;
        list->addItem(empty);
        frame->setContent(list);
        return frame;
    }

    auto* list = new tsl::elm::List();

    for (const auto& sink : sinks) {
        std::string dev_id   = device_id_;
        std::string name     = sink.name;
        std::string label    = sink.description[0] != '\0' ? std::string(sink.description) : std::string(sink.name);

        list->addItem(new tsl::elm::CategoryHeader(label));

        // Volume slider
        auto* slider = new tsl::elm::TrackBar(sym::volume, false, false, true, "Volume", "%");
        slider->setProgress(static_cast<u16>(sink.volume));

        // Mute toggle — must be created before wiring slider callback
        auto* mute = new tsl::elm::ToggleListItem("Mute", sink.is_muted);

        slider->setValueChangedListener([dev_id, name, mute](u16 vol) {
            kdecIpcSetVolumeSink(dev_id, name, static_cast<int32_t>(vol), mute->getState());
        });

        mute->setStateChangedListener([dev_id, name, slider](bool muted) {
            kdecIpcSetVolumeSink(dev_id, name, static_cast<int32_t>(slider->getProgress()), muted);
        });

        list->addItem(slider);
        list->addItem(mute);
    }

    frame->setContent(list);
    this->removeFocus();
    return frame;
}
