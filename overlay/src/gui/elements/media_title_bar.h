#pragma once
#include <tesla.hpp>
#include <string>
#include <vector>

class MediaTitleBar final : public tsl::elm::Element {
public:
    enum class Layout {
        Side, // album art 80x80 left, title/artist right  (Height = kHeightSide)
        Top,  // album art full-width centred at top, text below (Height = kHeightTop)
    };

    explicit MediaTitleBar(Layout layout = Layout::Side);

    void setInfo(const std::string& title, const std::string& artist);
    // Load and decode the raw image for this hash. Pass empty string to clear.
    // Retried on every call while the file isn't yet on disk.
    void setAlbumArt(const std::string& hash);

    // Actual height needed right now
    s32 height() const {
        if (m_layout == Layout::Side || m_art_pixels.empty()) return kHeightSide;
        return m_art_h + 84; // 4 top + art + 8 gap + title + 14 gap + artist + 18 bottom
    }

    void     draw(tsl::gfx::Renderer* renderer) override;
    void     layout(u16, u16, u16, u16) override;
    Element* requestFocus(Element*, tsl::FocusDirection) override { return nullptr; }
    void     drawFocusBackground(tsl::gfx::Renderer*) override {}
    void     drawHighlight(tsl::gfx::Renderer*)       override {}

    // Side layout: 80x80 thumbnail on the left.
    static constexpr s32 kArtDim    = 80;
    static constexpr s32 kArtGap    = 8;
    static constexpr s32 kHeightSide = 100;
    static constexpr s32 Height      = kHeightSide; // backward compat alias

    // Top layout: art above text, max dimension before ratio scaling.
    static constexpr s32 kArtTopDim  = 200;
    static constexpr s32 kHeightTop  = kArtTopDim + 70; // art + text area

private:
    struct ScrollState {
        u64   timeIn  = 0;
        u64   lastUpd = 0;
        float offset  = 0.0f;
        u32   maxW    = 0;
        u32   textW   = 0;
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

    Layout m_layout;

    std::string m_title;
    std::string m_artist;
    ScrollState m_titleScroll;
    ScrollState m_artistScroll;

    std::string          m_art_hash;
    std::vector<uint8_t> m_art_pixels;  // m_art_w * m_art_h * 4 RGBA8 bytes, or empty
    int                  m_art_w = 0;   // actual decoded / prescaled dimensions
    int                  m_art_h = 0;
};
