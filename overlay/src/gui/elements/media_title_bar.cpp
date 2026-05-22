#include "media_title_bar.h"

#include "../gui_common.h"

static constexpr int kTitleSize   = 22;
static constexpr int kArtistSize  = 18;
static constexpr int kScrollGap   = 40; // blank pixels between doubled text
static constexpr u8  kScrollPause = 90; // frames to pause before/after scroll

MediaTitleBar::MediaTitleBar() {
    m_isItem = false;
}

void MediaTitleBar::setInfo(const std::string& title, const std::string& artist) {
    if (title != m_title) {
        m_title        = title;
        m_title_scroll = m_title + std::string(8, ' ') + m_title;
        m_tw = 0; m_toff = 0; m_ttrunc = false; m_tctr = 0;
    }
    if (artist != m_artist) {
        m_artist        = artist;
        m_artist_scroll = m_artist + std::string(8, ' ') + m_artist;
        m_aw = 0; m_aoff = 0; m_atrunc = false; m_actr = 0;
    }
}

void MediaTitleBar::layout(u16, u16, u16, u16) {
    setBoundaries(getX(), getY(), getWidth(), Height);
}

void MediaTitleBar::draw(tsl::gfx::Renderer* renderer) {
    const s32 px = getX() + 20;
    const s32 pw = getWidth() - 40;

    // ---- Title ----
    const s32 titleY = getY() + 38;
    if (m_tw == 0 && !m_title.empty())
        m_tw = renderer->drawString(m_title.c_str(), false, 0, 0,
                                    kTitleSize, kTransparent).first;
    m_ttrunc = (m_tw > (u32)pw);
    if (m_ttrunc) {
        renderer->enableScissoring(px, titleY - kTitleSize - 4, pw, kTitleSize + 16);
        renderer->drawString(m_title_scroll.c_str(), false, px - (s32)m_toff,
                             titleY, kTitleSize, kWhite);
        renderer->disableScissoring();
        if (m_tctr > 0) --m_tctr;
        else if (++m_toff >= m_tw + kScrollGap) { m_toff = 0; m_tctr = kScrollPause; }
    } else {
        renderer->drawString(m_title.c_str(), false, px, titleY, kTitleSize, kWhite);
    }

    // ---- Artist ----
    if (!m_artist.empty()) {
        const s32 artistY = getY() + 74;
        if (m_aw == 0)
            m_aw = renderer->drawString(m_artist.c_str(), false, 0, 0,
                                        kArtistSize, kTransparent).first;
        m_atrunc = (m_aw > (u32)pw);
        if (m_atrunc) {
            renderer->enableScissoring(px, artistY - kArtistSize - 4, pw, kArtistSize + 16);
            renderer->drawString(m_artist_scroll.c_str(), false, px - (s32)m_aoff,
                                 artistY, kArtistSize, kDim);
            renderer->disableScissoring();
            if (m_actr > 0) --m_actr;
            else if (++m_aoff >= m_aw + kScrollGap) { m_aoff = 0; m_actr = kScrollPause; }
        } else {
            renderer->drawString(m_artist.c_str(), false, px, artistY, kArtistSize, kDim);
        }
    }
}