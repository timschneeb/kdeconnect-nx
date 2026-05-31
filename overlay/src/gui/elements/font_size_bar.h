#pragma once

#include <tesla.hpp>

// NamedStepTrackBar with an enabled/disabled state.
// When disabled: skipped in focus navigation, input is blocked, and a
// semi-transparent overlay is drawn on top to signal the inactive state.
class FontSizeBar : public tsl::elm::NamedStepTrackBar {
public:
    using NamedStepTrackBar::NamedStepTrackBar;

    void setEnabled(const bool enabled) { m_enabled = enabled; }
    bool isEnabled() const        { return m_enabled; }

    Element* requestFocus(Element* oldFocus,
                                    const tsl::FocusDirection direction) override {
        return m_enabled ? NamedStepTrackBar::requestFocus(oldFocus, direction) : nullptr;
    }

    bool handleInput(const u64 keysDown, const u64 keysHeld, const HidTouchState& touchPos,
                     const HidAnalogStickState left, const HidAnalogStickState right) override {
        if (!m_enabled) return false;
        return NamedStepTrackBar::handleInput(keysDown, keysHeld, touchPos, left, right);
    }

    bool onTouch(const tsl::elm::TouchEvent event, const s32 currX, const s32 currY,
                 const s32 prevX, const s32 prevY, const s32 initialX, const s32 initialY) override {
        if (!m_enabled) return false;
        return NamedStepTrackBar::onTouch(event, currX, currY, prevX, prevY, initialX, initialY);
    }

    void draw(tsl::gfx::Renderer* renderer) override {
        NamedStepTrackBar::draw(renderer);
        if (!m_enabled) {
            // Semi-transparent black overlay to dim the bar when it is inactive
            constexpr int topMargin = 8;
            renderer->drawRect(this->getX(), this->getY() + topMargin,
                               this->getWidth(), this->getHeight() - topMargin,
                               tsl::Color{0, 0, 0, 8});
        }
    }

private:
    bool m_enabled = true;
};
