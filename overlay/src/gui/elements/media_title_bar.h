#pragma once
#include <tesla.hpp>

class MediaTitleBar final : public tsl::elm::Element {
public:
    MediaTitleBar();

    void setInfo(const std::string& title, const std::string& artist);

    void     draw(tsl::gfx::Renderer* renderer) override;
    void     layout(u16, u16, u16, u16) override;
    Element* requestFocus(Element*, tsl::FocusDirection) override { return nullptr; }
    void     drawFocusBackground(tsl::gfx::Renderer*) override {}
    void     drawHighlight(tsl::gfx::Renderer*)       override {}

    static constexpr s32 Height = 100;

private:
    struct ScrollState {
        u64   timeIn  = 0;
        u64   lastUpd = 0;
        float offset  = 0.0f;
        u32   maxW    = 0;  // clip width; 0 = not yet measured
        u32   textW   = 0;  // width of scrollText (text + gap); the loop distance
        bool  active  = false;
        bool  trunc   = false;
        std::string scrollText;
    };

    static void resetScroll(ScrollState& s, std::string& dest, const std::string& src);
    void calcScrollWidth(tsl::gfx::Renderer* r, ScrollState& s,
                         const std::string& text, u32 fontSize);
    static void updateScroll(ScrollState& s);

    static void drawScrollText(tsl::gfx::Renderer* r, ScrollState& s,
                               const std::string& text,
                               s32 x, s32 y, s32 scissorY, u32 scissorH,
                               u32 fontSize, const tsl::Color& clr);

    std::string m_title;
    std::string m_artist;
    ScrollState m_titleScroll;
    ScrollState m_artistScroll;
};
