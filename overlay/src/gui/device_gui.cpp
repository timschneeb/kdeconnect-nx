#include "device_gui.h"
#include "commands_gui.h"
#include "main_gui.h"
#include "media_gui.h"
#include "volume_gui.h"
#include "gui_common.h"
#include <kdec/ipc_client.h>

#include "../utils/symbols.h"

DeviceGui::DeviceGui(const KdecDeviceInfo &dev) : dev_(dev) {}

void DeviceGui::update() {
    if (screenshot_ticks_ <= 0) return;
    if (--screenshot_ticks_ == 0) {
        tsl::gfx::Renderer::get().addScreenshotStacks(true);
    }
}

tsl::elm::Element* DeviceGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame(devIcon(dev_) + " " + dev_.name, statusStr(dev_));
    auto* list  = new tsl::elm::List();

    std::string id       = devId(dev_);
    const bool connected = dev_.is_connected;
    const bool paired    = (dev_.pair_state == DevicePairState::Paired);

    if (dev_.battery_level >= 0 && paired && connected) {
        list->addItem(new tsl::elm::CategoryHeader("Device info"));
        auto* battery = new tsl::elm::ListItem("Battery", batteryStr(dev_, true));
        battery->m_isItem = false;
        list->addItem(battery);
    }
    list->addItem(new tsl::elm::CategoryHeader("Actions"));

    switch (dev_.pair_state) {
        case DevicePairState::RequestedByPeer: {
            auto* accept = new tsl::elm::ListItem("Accept pair request");
            accept->setClickListener([id](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcAcceptPair(id); tsl::swapTo<MainGui>(SwapDepth{2}); return true; }
                return false;
            });
            list->addItem(accept);

            auto* reject = new tsl::elm::ListItem("Reject pair request");
            reject->setClickListener([id](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcRejectPair(id); tsl::swapTo<MainGui>(SwapDepth{2}); return true; }
                return false;
            });
            list->addItem(reject);
            break;
        }
        case DevicePairState::None: {
            auto* pair = new tsl::elm::ListItem("Request pair");
            pair->setClickListener([id](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcRequestPair(id); tsl::swapTo<MainGui>(SwapDepth{2}); return true; }
                return false;
            });
            list->addItem(pair);
            break;
        }
        case DevicePairState::RequestedByMe:
            list->addItem(new tsl::elm::SilentListItem("Pairing in progress..."));
            break;
        default:
            break;
    }

    if (paired && connected) {
        std::string name = devName(dev_);

        if (dev_.supports_mpris_remote) {
            auto* media = new tsl::elm::ListItem("Media remote", sym::chevronRight);
            media->setClickListener([id, name](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { tsl::changeTo<MediaGui>(id, name); return true; }
                return false;
            });
            list->addItem(media);
        }

        if (dev_.supports_volume_sinks) {
            auto* vol = new tsl::elm::ListItem("Audio devices", sym::chevronRight);
            vol->setClickListener([id, name](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { tsl::changeTo<VolumeGui>(id, name); return true; }
                return false;
            });
            list->addItem(vol);
        }

        if (dev_.supports_commands) {
            auto* cmds = new tsl::elm::ListItem("Commands", sym::chevronRight);
            cmds->setClickListener([id, name](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { tsl::changeTo<CommandsGui>(id, name); return true; }
                return false;
            });
            list->addItem(cmds);
        }

        if (dev_.supports_share) {
            auto* screenshot = new tsl::elm::ListItem("Send screenshot");
            screenshot->setClickListener([this, id](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    // Remove overlay from screenshot layer stack before capture, then add it back after a short delay
                    // to be safe that the screenshot has completed.
                    screenshot_ticks_ = 1;
                    tsl::gfx::Renderer::get().removeScreenshotStacks(true);
                    kdecIpcSendScreenshot(dev_.id);
                    return true;
                }
                return false;
            });
            list->addItem(screenshot);
        }

        auto* ping = new tsl::elm::ListItem("Ping");
        ping->setClickListener([id](const u64 keys) -> bool {
            if (keys & HidNpadButton_A) { kdecIpcPing(id); return true; }
            return false;
        });
        list->addItem(ping);

        if (dev_.supports_find_my_phone) {
            auto* ring = new tsl::elm::ListItem("Ring");
            ring->setClickListener([id](const u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcRing(id); return true; }
                return false;
            });
            list->addItem(ring);
        }
    }

    if (paired) {
        auto* unpair = new tsl::elm::ListItem("Unpair");
        unpair->setValue(sym::cancel, true);
        unpair->setClickListener([id](const u64 keys) -> bool {
            if (keys & HidNpadButton_A) { kdecIpcUnpair(id); tsl::swapTo<MainGui>(SwapDepth{2}); return true; }
            return false;
        });
        list->addItem(unpair);
    }

    frame->setContent(list);
    return frame;
}
