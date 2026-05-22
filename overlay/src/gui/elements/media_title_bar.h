#pragma once
#include <tesla.hpp>

class MediaTitleBar final : public tsl::elm::Element {
public:
    MediaTitleBar();

    void setInfo(const std::string& title, const std::string& artist);

    void               draw(tsl::gfx::Renderer* renderer) override;
    void               layout(u16 px, u16 py, u16 pw, u16) override;
    Element*           requestFocus(Element*, tsl::FocusDirection) override { return nullptr; }
    void               drawFocusBackground(tsl::gfx::Renderer*) override {}
    void               drawHighlight(tsl::gfx::Renderer*)       override {}

    static constexpr s32 Height = 100;

private:
    std::string m_title;
    std::string m_artist;

    // Scroll state: title
    std::string m_title_scroll;
    u32  m_tw    = 0;
    u32  m_toff  = 0;
    bool m_ttrunc = false;
    u8   m_tctr  = 0;

    // Scroll state: artist
    std::string m_artist_scroll;
    u32  m_aw    = 0;
    u32  m_aoff  = 0;
    bool m_atrunc = false;
    u8   m_actr  = 0;
};