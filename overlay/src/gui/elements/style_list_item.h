#pragma once

#include <tesla.hpp>

// Two-row list item: label on the first line, smaller dim description below.
// Renders a checkmark (right-aligned) when selected.
class StyleListItem : public tsl::elm::ListItem {
    static constexpr u16    kHeight  = 88;
    static constexpr u32    kNameSz  = 22;
    static constexpr u32    kDescSz  = 20;
    static constexpr tsl::Color kDescColor{0xD, 0xD, 0xD, 0xD};

    std::string m_desc;

public:
    StyleListItem(const std::string& name, const std::string& desc, bool selected)
        : tsl::elm::ListItem(name, selected ? sym::accept : "") {
        m_desc = desc;
        m_listItemHeight = kHeight;
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        // Touch-press highlight
        const bool touched = m_touched
            && tsl::elm::Element::getInputMode() == tsl::InputMode::Touch
            && ult::touchInBounds;
        if (touched && !m_flags.m_isTouchHolding)
            renderer->drawRectAdaptive(this->getX() + 4, this->getY(),
                                       this->getWidth() - 8, this->getHeight(),
                                       aWithOpacity(tsl::clickColor));

        // Separator lines
        const float top    = this->getTopBound();
        const float bottom = this->getBottomBound();
        static float s_lastBottom = 0.0f;
        if (s_lastBottom != top)
            renderer->drawRect(this->getX() + 4, top,
                               this->getWidth() + 10, 1, a(tsl::separatorColor));
        renderer->drawRect(this->getX() + 4, bottom,
                           this->getWidth() + 10, 1, a(tsl::separatorColor));
        s_lastBottom = bottom;

        const s32 textX = this->getX() + 19;
        // Row 1: style name
        renderer->drawString(m_text_clean, false, textX, this->getY() + 36, kNameSz, tsl::defaultTextColor);

        // Row 2: description (smaller, dim)
        renderer->drawString(m_desc, false, textX, this->getY() + 67, kDescSz, a(kDescColor));

        // Checkmark, right-aligned and vertically centered on row 1
        if (!m_value.empty()) {
            const s32 valW = renderer->getTextDimensions(m_value, false, kNameSz).first;
            renderer->drawString(m_value, false,
                this->getX() + this->getWidth() - valW - 20,
                this->getY() + 53, kNameSz, a(tsl::onTextColor));
        }
    }
};
