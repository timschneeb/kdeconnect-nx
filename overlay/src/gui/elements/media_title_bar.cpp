#include "media_title_bar.h"

#include "../gui_common.h"

static constexpr u32 kTitleSize  = 22;
static constexpr u32 kArtistSize = 18;

MediaTitleBar::MediaTitleBar() {
    m_isItem = false;
}

void MediaTitleBar::setInfo(const std::string& title, const std::string& artist) {
    resetScroll(m_titleScroll,  m_title,  title);
    resetScroll(m_artistScroll, m_artist, artist);
}

void MediaTitleBar::layout(u16, u16, u16, u16) {
    setBoundaries(getX(), getY(), getWidth(), Height);
}

void MediaTitleBar::resetScroll(ScrollState& s, std::string& dest, const std::string& src) {
    if (dest == src) return;
    dest = src;
    s = ScrollState{};
}

void MediaTitleBar::calcScrollWidth(tsl::gfx::Renderer* r, ScrollState& s,
                                     const std::string& text, u32 fontSize) {
    if (s.maxW) return;
    s.maxW = static_cast<u32>(getWidth());
    const u32 w = r->getTextDimensions(text, false, fontSize).first;
    s.trunc = w > s.maxW;
    if (s.trunc) {
        s.scrollText = text + "        ";
        s.textW      = r->getTextDimensions(s.scrollText, false, fontSize).first;
        s.scrollText += text;
    } else {
        s.textW = w;
    }
}

void MediaTitleBar::updateScroll(ScrollState& s) {
    const u64 now = ult::nowNs();
    if (now - s.lastUpd < 8333333ULL) return; // cap at ~120 Hz

    static constexpr double delay    = 3.0;
    static constexpr double pause    = 2.0;
    static constexpr double vel      = 100.0;
    static constexpr double accel    = 0.5;
    static constexpr double decel    = 0.5;
    static constexpr double invBil   = 1e-9;
    static constexpr double invAccel = 2.0;
    static constexpr double invDecel = 2.0;

    const double minDist   = s.textW;
    const double accelDist = 0.5 * vel * accel;
    const double decelDist = 0.5 * vel * decel;
    const double constDist = std::max(0.0, minDist - accelDist - decelDist);
    const double constTime = constDist / vel;
    const double totalDur  = delay + accel + constTime + decel + pause;

    const double t     = (now - s.timeIn) * invBil;
    const double cycle = std::fmod(t, totalDur);

    if (cycle < delay) {
        s.offset = 0.0f;
    } else if (cycle < delay + accel + constTime + decel) {
        const double st = cycle - delay;
        double d;
        if (st <= accel) {
            const double r = st * invAccel;
            d = r * r * accelDist;
        } else if (st <= accel + constTime) {
            d = accelDist + (st - accel) * vel;
        } else {
            const double r   = (st - accel - constTime) * invDecel;
            const double omr = 1.0 - r;
            d = accelDist + constDist + (1.0 - omr * omr) * (minDist - accelDist - constDist);
        }
        s.offset = static_cast<float>(std::min(d, minDist));
    } else {
        s.offset = static_cast<float>(s.textW);
    }

    s.lastUpd = now;
    if (t >= totalDur) s.timeIn = now;
}

void MediaTitleBar::drawScrollText(tsl::gfx::Renderer* r, ScrollState& s,
                                    const std::string& text,
                                    s32 x, s32 y, s32 scissorY, u32 scissorH,
                                    u32 fontSize, const tsl::Color& clr) {
    if (s.trunc) {
        if (!s.active) { s.active = true; s.timeIn = ult::nowNs(); }
        r->enableScissoring(x, scissorY, s.maxW, scissorH);
        r->drawString(s.scrollText.c_str(), false, x - static_cast<s32>(s.offset), y, fontSize, clr);
        r->disableScissoring();
        updateScroll(s);
    } else {
        r->drawString(text.c_str(), false, x, y, fontSize, clr);
    }
}

void MediaTitleBar::draw(tsl::gfx::Renderer* renderer) {
    const s32 px = getX();
    const s32 py = getY();

    if (!m_title.empty()) {
        const s32 titleY = py + 38;
        calcScrollWidth(renderer, m_titleScroll, m_title, kTitleSize);
        drawScrollText(renderer, m_titleScroll, m_title,
                       px, titleY,
                       titleY - static_cast<s32>(kTitleSize) - 4, kTitleSize + 16,
                       kTitleSize, kWhite);
    }

    if (!m_artist.empty()) {
        const s32 artistY = py + 74;
        calcScrollWidth(renderer, m_artistScroll, m_artist, kArtistSize);
        drawScrollText(renderer, m_artistScroll, m_artist,
                       px, artistY,
                       artistY - static_cast<s32>(kArtistSize) - 4, kArtistSize + 16,
                       kArtistSize, kDim);
    }
}
