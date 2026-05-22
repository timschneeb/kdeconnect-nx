#include "error_widget.h"

namespace ErrorWidget {
    tsl::elm::List *create(
        const std::string& symbol, const std::string& text,
        const std::optional<std::string>& button_text ,
        const std::function<bool(u64 keys)>& button_listener) {

        auto *list = new tsl::elm::List();
        auto warning = new tsl::elm::CustomDrawer([symbol, text](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
            static const auto iconWidth = renderer->getTextDimensions(symbol, false, 90).first;
            static const auto textWidth = renderer->getTextDimensions(text, false, 25).first;
            renderer->drawString(symbol, false, (tsl::cfg::FramebufferWidth - iconWidth) / 2, 250, 90, 0xFFFF);
            renderer->drawString(text, false, (tsl::cfg::FramebufferWidth - textWidth) / 2, 340, 25,0xFFFF);
        });
        list->addItem(warning, tsl::cfg::FramebufferHeight - tsl::style::ListItemDefaultHeight - 256);

        if (button_text.has_value()) {
            auto *retryItem = new tsl::elm::ListItem(button_text.value());
            retryItem->setClickListener(button_listener);
            list->addItem(retryItem);
        }

        return list;
    }
}