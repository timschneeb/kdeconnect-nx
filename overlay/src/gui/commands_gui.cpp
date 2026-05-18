#include "commands_gui.h"
#include "gui_common.h"
#include <kdec/ipc_client.h>
#include <cstdio>
#include <vector>

#include "../utils/symbols.h"

CommandsGui::CommandsGui(std::string device_id, std::string device_name)
    : device_id_(std::move(device_id)), device_name_(std::move(device_name)) {}

tsl::elm::Element* CommandsGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Commands", device_name_);

    std::vector<KdecCommandEntry> commands;
    Result rc = kdecIpcGetCommandList(device_id_, commands);

    if (R_FAILED(rc)) {
        char info[48];
        snprintf(info, sizeof(info), "[0x%08X] %04d-%04d", rc, R_MODULE(rc), R_DESCRIPTION(rc));
        std::string info_s(info);
        frame->setContent(new tsl::elm::CustomDrawer([info_s](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString(sym::errorCircleFilled, false, x + 40,  y + 50, 40, TEXT_COLOR);
            renderer->drawString("IPC Error",    false, x + 105, y + 50, 28, TEXT_COLOR);
            renderer->drawString("Could not fetch commands\nfrom the sysmodule.", false, x + 40, y + 100, 20, TEXT_COLOR);
            renderer->drawString(info_s.c_str(), false, x + 40,  y + 148, 16, DESC_COLOR);
        }));
    } else if (commands.empty()) {
        auto* list = new tsl::elm::List();
        auto* no_cmd = new tsl::elm::SilentListItem("No commands defined");
        no_cmd->setFocused(false);
        no_cmd->m_isItem = false;

        list->addItem(no_cmd);
        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
            auto lineHeight = tsl::gfx::FontManager::getFontMetricsForCharacter('A', 20).lineHeight + 5;
            renderer->drawString("You can add commands in the", false, x + 5, y + 50, 20, DESC_COLOR);
            renderer->drawString("KDE Connect command plugin", false, x + 5, y + lineHeight + 50, 20, DESC_COLOR);
            renderer->drawString("settings on the remote device.", false, x + 5, y + lineHeight*2 + 50, 20, DESC_COLOR);
        }));
        frame->setContent(list);
    } else {
        auto* list = new tsl::elm::List();
        for (const auto& cmd : commands) {
            std::string cmd_id(cmd.id);
            std::string cmd_name(cmd.name);
            std::string dev_id = device_id_;

            auto* item = new tsl::elm::ListItem(cmd_name);
            item->setClickListener([dev_id, cmd_id](u64 keys) -> bool {
                if (keys & HidNpadButton_A) {
                    kdecIpcRunCommand(dev_id, cmd_id);
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }
        frame->setContent(list);
    }

    this->removeFocus();
    return frame;
}
