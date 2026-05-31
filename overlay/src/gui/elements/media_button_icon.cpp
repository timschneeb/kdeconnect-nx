#include "media_button_icon.h"

#include "../gui_common.h"
#include "../../utils/media_symbols.hpp"

MediaIconButton::MediaIconButton(const AlphaSymbol* icon, std::function<void()> action)
    : m_icon(icon), m_action(std::move(action))
{
    m_isItem = false;
}

tsl::elm::Element* MediaIconButton::requestFocus(Element*, const tsl::FocusDirection dir) {
    return (dir == tsl::FocusDirection::None) ? this : nullptr;
}

bool MediaIconButton::onClick(const u64 keys) {
    if ((keys & HidNpadButton_A) && !m_disabled && m_action) {
        m_action();
        triggerClickAnimation();
        return true;
    }
    return false;
}

void MediaIconButton::draw(tsl::gfx::Renderer* renderer) {
    const s32 cx = getX() + getWidth()  / 2;
    const s32 cy = getY() + getHeight() / 2;

    if (m_icon) {
        tsl::Color iconCol = m_disabled
            ? tsl::gfx::Renderer::a(tsl::Color{0x4, 0x4, 0x4, 0xF})
            : kDim;
        m_icon->draw(cx, cy, renderer, iconCol);
    }
}

void MediaIconButton::drawFocusBackground(tsl::gfx::Renderer*) {
    if (this->m_clickAnimationProgress > 0) {
        float rem = tsl::style::ListItemHighlightLength *
            (1.0f - ((ult::nowNs() - this->m_animationStartTime) * 0.000001f) * 0.002f);
        if (rem < 0.0f) rem = 0.0f;
        this->m_clickAnimationProgress = static_cast<u8>(rem);
    }
}

void MediaIconButton::drawHighlight(tsl::gfx::Renderer* renderer) {
    if (!this->m_focused) return;

    const u64 now_ns = ult::nowNs();
    const double p = (ult::cos(2.0 * ult::_M_PI *
        std::fmod(now_ns * 0.000000001 - 0.25, 1.0)) + 1.0) * 0.5;

    tsl::s_highlightColor = {
        static_cast<u8>(tsl::highlightColor2.r +
            (tsl::highlightColor1.r - tsl::highlightColor2.r) * p + 0.5),
        static_cast<u8>(tsl::highlightColor2.g +
            (tsl::highlightColor1.g - tsl::highlightColor2.g) * p + 0.5),
        static_cast<u8>(tsl::highlightColor2.b +
            (tsl::highlightColor1.b - tsl::highlightColor2.b) * p + 0.5),
        0xF
    };

    const auto hl   = tsl::gfx::Renderer::a(tsl::s_highlightColor);
    const s32  cx   = getX() + getWidth()  / 2;
    const s32  cy   = getY() + getHeight() / 2;
    const s32  base = std::min(getWidth(), getHeight()) / 2;

    renderer->drawCircle(cx, cy, base + 5, true, hl);
    // Cut out the center with the menu background to leave a thick ring.
    renderer->drawCircle(cx, cy, base, true, tsl::gfx::Renderer::a(tsl::defaultBackgroundColor));
}