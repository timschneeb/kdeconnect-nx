#pragma once
#include "tesla.hpp"

class AlphaSymbol;
class MediaIconButton;

class MediaButtonRow final : public tsl::elm::Element {
public:
    // Button indices
    enum : int { IDX_PREV = 0, IDX_REWIND = 1, IDX_PLAY = 2, IDX_FFORWARD = 3, IDX_NEXT = 4 };

    MediaButtonRow();
    ~MediaButtonRow();

    // Configure a single button (call in createUI after construction).
    void setButton(int idx, const AlphaSymbol* icon, std::function<void()> action,
                   bool disabled = false) const;
    void setButtonDisabled(int idx, bool disabled) const;
    void setButtonIcon(int idx, const AlphaSymbol* icon) const;

    // tsl::elm::Element overrides
    tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection dir) override;
    bool               handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&,
                                   HidAnalogStickState leftStick,
                                   HidAnalogStickState rightStick) override;
    void               draw(tsl::gfx::Renderer* renderer) override;
    void               layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override;
    void               drawFocusBackground(tsl::gfx::Renderer*) override {}
    void               drawHighlight(tsl::gfx::Renderer*)       override {}

    static constexpr s32 Height = tsl::style::ListItemDefaultHeight + 20;

private:
    MediaIconButton* m_buttons[5] = {};
    int              m_active     = IDX_PLAY;   // focused button within the row
    int8_t           m_stick_dir  = 0;          // -1 left 0 neutral +1 right (joystick edge detect)
};
