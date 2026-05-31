#include "media_button_row.h"

#include "media_button_icon.h"

MediaButtonRow::MediaButtonRow() {
    m_isItem = true;
    for (auto& btn : m_buttons)
        btn = new MediaIconButton(nullptr, nullptr);
}

MediaButtonRow::~MediaButtonRow() {
    for (auto* b : m_buttons) delete b;
}

void MediaButtonRow::setButton(const int idx, const AlphaSymbol* icon,
                               std::function<void()> action, const bool disabled) const {
    if (idx < 0 || idx >= 5 || !m_buttons[idx]) return;
    m_buttons[idx]->setIcon(icon);
    m_buttons[idx]->setAction(std::move(action));
    m_buttons[idx]->setDisabled(disabled);
}

void MediaButtonRow::setButtonDisabled(const int idx, const bool disabled) const {
    if (idx >= 0 && idx < 5 && m_buttons[idx])
        m_buttons[idx]->setDisabled(disabled);
}

void MediaButtonRow::setButtonIcon(const int idx, const AlphaSymbol* icon) const {
    if (idx >= 0 && idx < 5 && m_buttons[idx])
        m_buttons[idx]->setIcon(icon);
}

tsl::elm::Element* MediaButtonRow::requestFocus(Element*,
                                                const tsl::FocusDirection dir) {
    if (dir == tsl::FocusDirection::None ||
        dir == tsl::FocusDirection::Up   ||
        dir == tsl::FocusDirection::Down)
        return this;
    return nullptr;
}

bool MediaButtonRow::handleInput(const u64 keysDown, u64, const HidTouchState&,
                                 const HidAnalogStickState leftStick, HidAnalogStickState) {
    static constexpr s32 kStickThreshold = 16384;
    const int8_t new_dir = (leftStick.x >  kStickThreshold) ?  1
                         : (leftStick.x < -kStickThreshold) ? -1 : 0;
    const bool stick_left  = (new_dir == -1 && m_stick_dir != -1);
    const bool stick_right = (new_dir ==  1 && m_stick_dir !=  1);
    m_stick_dir = new_dir;

    if ((keysDown & HidNpadButton_Left) || stick_left) {
        if (m_active > 0) { --m_active; return true; }
        return false;
    }
    if ((keysDown & HidNpadButton_Right) || stick_right) {
        if (m_active < 4) { ++m_active; return true; }
        return false;
    }
    if (keysDown & HidNpadButton_A) {
        if (m_buttons[m_active] && !m_buttons[m_active]->isDisabled())
            m_buttons[m_active]->onClick(HidNpadButton_A);
        return true;
    }
    return false;
}

void MediaButtonRow::draw(tsl::gfx::Renderer* renderer) {
    for (int i = 0; i < 5; i++) {
        if (!m_buttons[i]) continue;
        m_buttons[i]->setFocused(m_focused && (m_active == i));
        m_buttons[i]->frame(renderer);
    }
}

void MediaButtonRow::layout(u16, u16, u16, u16) {
    setBoundaries(getX(), getY(), getWidth(), Height);

    const s32 cy = getY() + Height / 2;
    const s32 gx = getX();
    const s32 gw = getWidth();
    const s32 bxc[5] = {
        gx + gw / 6,     gx + gw * 2 / 6, gx + gw / 2,
        gx + gw * 4 / 6, gx + gw * 5 / 6,
    };
    static constexpr s32 kRadii[5] = { 18, 18, 22, 18, 18 };
    for (int i = 0; i < 5; i++) {
        if (!m_buttons[i]) continue;
        const s32 r = kRadii[i] + 6;
        m_buttons[i]->setBoundaries(bxc[i] - r, cy - r, r * 2, r * 2);
    }
}