#pragma once

#include <tesla.hpp>

class LargeIconListItem : public tsl::elm::ListItem {
    static constexpr u16 kHeight = 105;
    std::string m_line2;
    std::string m_icon;

public:
    LargeIconListItem(const std::string& line1, const std::string& line2, const std::string& action, const std::string& icon)
        : tsl::elm::ListItem(line1, action) {
        m_line2 = line2;
        m_icon = icon;
        m_listItemHeight = kHeight;
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        // Separator lines
        const float topBound    = this->getTopBound();
        const float bottomBound = this->getBottomBound();
        static float lastBottomBound = 0.0f;
        if (lastBottomBound != topBound)
            renderer->drawRect(this->getX() + 4, topBound,    this->getWidth() + 10, 1, a(tsl::separatorColor));
        renderer->drawRect(    this->getX() + 4, bottomBound, this->getWidth() + 10, 1, a(tsl::separatorColor));
        lastBottomBound = bottomBound;

        static constexpr tsl::Color kActionColor{0x9, 0x9, 0x9, 0xF};

        s32 textX = this->getX() + 19;
        int icon_width = renderer->drawString(m_icon, false, textX, getY() + 50, 32, a(tsl::defaultTextColor)).first;
        textX += icon_width + 19;

        renderer->drawString(m_text_clean, false, textX, this->getY() + 30, 20, a(tsl::defaultTextColor));
        renderer->drawString(m_line2,      false, textX, this->getY() + 60, 20, a(tsl::defaultTextColor));
        renderer->drawString(m_value,      false, textX, this->getY() + 90, 18, a(kActionColor));
    }
};
