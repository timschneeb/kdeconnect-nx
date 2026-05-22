#pragma once
#include "tesla.hpp"

class AlphaSymbol;

class MediaIconButton final : public tsl::elm::Element {
public:
    MediaIconButton(const AlphaSymbol* icon, std::function<void()> action);

    Element* requestFocus(Element* oldFocus, tsl::FocusDirection dir) override;
    bool               onClick(u64 keys) override;
    void               draw(tsl::gfx::Renderer* renderer) override;
    void               layout(u16, u16, u16, u16) override {}          // bounds set by row
    void               drawFocusBackground(tsl::gfx::Renderer* renderer) override; // decay only
    void               drawHighlight(tsl::gfx::Renderer* renderer) override;       // circle

    void setIcon(const AlphaSymbol* icon)            { m_icon = icon; }
    void setAction(std::function<void()> action)     { m_action = std::move(action); }
    void setDisabled(const bool disabled)            { m_disabled = disabled; }
    bool isDisabled() const                          { return m_disabled; }

private:
    const AlphaSymbol*    m_icon;
    std::function<void()> m_action;
    bool                  m_disabled = false;
};
