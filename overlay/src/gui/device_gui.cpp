#include "device_gui.h"
#include "commands_gui.h"
#include "volume_gui.h"
#include "gui_common.h"
#include <kdec/ipc_client.h>

#include "../utils/symbols.h"

DeviceGui::DeviceGui(KdecDeviceInfo dev) : dev_(dev) {}

tsl::elm::Element* DeviceGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame(devName(dev_), statusStr(dev_));
    auto* list  = new tsl::elm::List();

    if (dev_.battery_level >= 0) {
        list->addItem(new tsl::elm::CategoryHeader("Device Info"));
        auto* battery = new tsl::elm::ListItem("Battery", batteryStr(dev_));
        battery->m_isItem = false;
        list->addItem(battery);
    }
    list->addItem(new tsl::elm::CategoryHeader("Actions"));

    std::string id       = devId(dev_);
    const bool connected = dev_.is_connected;
    const bool paired    = (dev_.pair_state == DevicePairState::Paired);

    switch (dev_.pair_state) {
        case DevicePairState::RequestedByPeer: {
            auto* accept = new tsl::elm::ListItem("Accept Pair Request");
            accept->setClickListener([id](u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcAcceptPair(id); tsl::goBack(); return true; }
                return false;
            });
            list->addItem(accept);

            auto* reject = new tsl::elm::ListItem("Reject Pair Request");
            reject->setClickListener([id](u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcRejectPair(id); tsl::goBack(); return true; }
                return false;
            });
            list->addItem(reject);
            break;
        }
        case DevicePairState::None: {
            auto* pair = new tsl::elm::ListItem("Request Pair");
            pair->setClickListener([id](u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcRequestPair(id); tsl::goBack(); return true; }
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

        if (dev_.supports_volume_sinks) {
            auto* vol = new tsl::elm::ListItem("Volume", sym::chevronRight);
            vol->setClickListener([id, name](u64 keys) -> bool {
                if (keys & HidNpadButton_A) { tsl::changeTo<VolumeGui>(id, name); return true; }
                return false;
            });
            list->addItem(vol);
        }

        if (dev_.supports_commands) {
            auto* cmds = new tsl::elm::ListItem("Commands", sym::chevronRight);
            cmds->setClickListener([id, name](u64 keys) -> bool {
                if (keys & HidNpadButton_A) { tsl::changeTo<CommandsGui>(id, name); return true; }
                return false;
            });
            list->addItem(cmds);
        }

        auto* ping = new tsl::elm::ListItem("Ping");
        ping->setClickListener([id](u64 keys) -> bool {
            if (keys & HidNpadButton_A) { kdecIpcPing(id); return true; }
            return false;
        });
        list->addItem(ping);

        if (dev_.supports_find_my_phone) {
            auto* ring = new tsl::elm::ListItem("Ring");
            ring->setClickListener([id](u64 keys) -> bool {
                if (keys & HidNpadButton_A) { kdecIpcRing(id); return true; }
                return false;
            });
            list->addItem(ring);
        }
    }

    if (paired) {
        auto* unpair = new tsl::elm::ListItem("Unpair");
        unpair->setClickListener([id](u64 keys) -> bool {
            if (keys & HidNpadButton_A) { kdecIpcUnpair(id); tsl::goBack(); return true; }
            return false;
        });
        list->addItem(unpair);
    }

    frame->setContent(list);
    return frame;
}
