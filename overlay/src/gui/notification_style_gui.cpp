#include "notification_style_gui.h"
#include "gui_common.h"
#include "../utils/symbols.h"
#include "elements/style_list_item.h"
#include <kdec/ipc_client.h>

#include "settings_gui.h"

namespace {
    struct StyleEntry {
        NotificationStyle  value;
        const char*        label;
        const char*        description; // shown as value/subtitle
    };
    constexpr StyleEntry kStyles[] = {
        { NotificationStyle::AppName_TitleBody, "App name", "Title: Message" },
        { NotificationStyle::AppName_Title,     "App name","Title" },
        { NotificationStyle::Title_Body,        "Title", "Message" },
        { NotificationStyle::TitleAppName_Body, "Title \xc2\xb7 App name", "Message" },
        { NotificationStyle::AppNameTitle_Body, "App name \xc2\xb7 Title", "Message" },
    };
}

tsl::elm::Element* NotificationStyleGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Notification style", "KDE Connect NX");
    auto* list  = new tsl::elm::List();

    int32_t cur_raw = 0;
    kdecIpcReadIntSetting(KdecIntSettingKey::NotificationStyle, cur_raw);
    const auto current = static_cast<NotificationStyle>(cur_raw);

    for (const auto& entry : kStyles) {
        const bool selected = (entry.value == current);
        auto* item = new StyleListItem(entry.label, entry.description, selected);
        const auto val = entry.value;
        item->setClickListener([val](u64 keys) -> bool {
            if (keys & HidNpadButton_A) {
                kdecIpcWriteIntSetting(KdecIntSettingKey::NotificationStyle,
                                       static_cast<int32_t>(val));
                tsl::swapTo<SettingsGui>(SwapDepth{2});
                return true;
            }
            return false;
        });
        list->addItem(item);
    }

    frame->setContent(list);
    return frame;
}

bool NotificationStyleGui::handleInput(u64 keysDown, u64, const HidTouchState&,
                                        HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::goBack();
        return true;
    }
    return false;
}
