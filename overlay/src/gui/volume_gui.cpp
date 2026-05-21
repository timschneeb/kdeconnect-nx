#include "volume_gui.h"
#include "gui_common.h"
#include <kdec/ipc_client.h>
#include <algorithm>
#include <cstdio>
#include <memory>
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

    // Active sink first, rest in original order
    std::stable_sort(sinks.begin(), sinks.end(), [](const KdecVolumeSinkInfo& a, const KdecVolumeSinkInfo& b) {
        return a.is_default_output > b.is_default_output;
    });

    auto* list = new tsl::elm::List();

    // Shared across all "Set as Default" buttons so each click can update the rest
    auto all_default_btns = std::make_shared<std::vector<tsl::elm::ListItem*>>();

    for (size_t i = 0; i < sinks.size(); i++) {
        const auto& sink = sinks[i];
        std::string dev_id = device_id_;
        std::string name   = sink.name;
        std::string label  = sink.description[0] != '\0' ? std::string(sink.description) : std::string(sink.name);

        auto* header = new tsl::elm::CategoryHeader(label);
        header->setValue(std::string(sym::yButton) + " Mute", tsl::bannerVersionTextColor);
        list->addItem(header);

        auto* slider = new tsl::elm::TrackBar(sym::volume, false, false, true, "Volume", "%");
        slider->setProgress(static_cast<u16>(sink.volume));

        auto* mute = new tsl::elm::ToggleListItem("Mute", sink.is_muted);

        slider->setValueChangedListener([dev_id, name, mute](u16 vol) {
            kdecIpcSetVolumeSink(dev_id, name, static_cast<int32_t>(vol), mute->getState());
        });

        mute->setStateChangedListener([dev_id, name, slider](bool muted) {
            kdecIpcSetVolumeSink(dev_id, name, static_cast<int32_t>(slider->getProgress()), muted);
        });

        list->addItem(slider);
        list->addItem(mute);

        // "Set as Default" doubles as an active indicator:
        //   active  → value "✓", m_isItem=false (non-interactive, visually dimmed)
        //   inactive → value "",  m_isItem=true  (interactive)
        auto* default_btn = new tsl::elm::ListItem("Set as Default", sink.is_default_output ? sym::accept : "");
        default_btn->m_isItem = !sink.is_default_output;
        all_default_btns->push_back(default_btn);

        default_btn->setClickListener([dev_id, name, slider, mute, all_default_btns, i](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                kdecIpcSetVolumeSink(dev_id, name,
                    static_cast<int32_t>(slider->getProgress()),
                    mute->getState(),
                    /*is_default_output=*/true);

                for (size_t j = 0; j < all_default_btns->size(); j++) {
                    auto* btn = (*all_default_btns)[j];
                    btn->setValue(j == i ? sym::accept : "");
                    btn->m_isItem = (j != i);
                }
                return true;
            }
            return false;
        });

        list->addItem(default_btn);
    }

    frame->setContent(list);
    this->removeFocus();
    return frame;
}
