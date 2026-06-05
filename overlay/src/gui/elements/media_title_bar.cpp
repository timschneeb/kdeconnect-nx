#include "media_title_bar.h"

#include "../gui_common.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_BMP
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <webp/decode.h>

static constexpr u32 kTitleSize  = 22;
static constexpr u32 kArtistSize = 18;

MediaTitleBar::MediaTitleBar(const Layout layout) : m_layout(layout) {
    m_isItem = false;
}

void MediaTitleBar::setInfo(const std::string& title, const std::string& artist) {
    resetScroll(m_titleScroll,  m_title,  title);
    resetScroll(m_artistScroll, m_artist, artist);
}

void MediaTitleBar::setAlbumArt(const std::string& hash) {
    // Already displaying art for this hash
    if (!m_art_pixels.empty() && hash == m_art_hash) return;

    // Hash changed: reset display state and scroll layout.
    if (hash != m_art_hash) {
        m_art_hash = hash;
        m_art_pixels.clear();
        m_art_w = m_art_h = 0;
        m_titleScroll  = {};
        m_artistScroll = {};
        // Height dropped back to compact, re-layout.
        if (auto* p = getParent()) p->invalidate();
        if (hash.empty()) return;
    }

    // Attempt loading the raw image file.
    char path[128];
    snprintf(path, sizeof(path), "/config/kdeconnect/album_art/%s", hash.c_str());
    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return; }

    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    fread(buf.data(), 1, buf.size(), f);
    fclose(f);

    int src_w = 0, src_h = 0;
    uint8_t* decoded = nullptr;
    bool is_webp = false;

    if (buf.size() >= 12 &&
        memcmp(buf.data(), "RIFF", 4) == 0 &&
        memcmp(buf.data() + 8, "WEBP", 4) == 0) {
        is_webp = true;
        decoded = WebPDecodeRGBA(buf.data(), buf.size(), &src_w, &src_h);
    } else {
        int ch = 0;
        decoded = stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()),
                                        &src_w, &src_h, &ch, 4);
    }

    if (!decoded || src_w <= 0 || src_h <= 0) {
        if (decoded) { if (is_webp) WebPFree(decoded); else stbi_image_free(decoded); }
        return;
    }

    // Compute prescale target preserving aspect ratio.
    int dst_w, dst_h;
    if (m_layout == Layout::Top) {
        // Scale so the larger dimension equals kArtTopDim.
        const float scale = std::min(static_cast<float>(kArtTopDim) / src_w,
                                     static_cast<float>(kArtTopDim) / src_h);
        dst_w = std::max(1, static_cast<int>(src_w * scale));
        dst_h = std::max(1, static_cast<int>(src_h * scale));
    } else {
        // Side layout: fixed square thumbnail.
        dst_w = dst_h = kArtDim;
    }

    // Nearest-neighbor scale to (dst_w * dst_h).
    m_art_pixels.resize(static_cast<size_t>(dst_w) * dst_h * 4);
    for (int oy = 0; oy < dst_h; ++oy) {
        const int sy = oy * src_h / dst_h;
        for (int ox = 0; ox < dst_w; ++ox) {
            const int sx  = ox * src_w / dst_w;
            const int src = (sy * src_w + sx) * 4;
            const int dst = (oy * dst_w + ox) * 4;
            m_art_pixels[dst]     = decoded[src];
            m_art_pixels[dst + 1] = decoded[src + 1];
            m_art_pixels[dst + 2] = decoded[src + 2];
            m_art_pixels[dst + 3] = decoded[src + 3];
        }
    }
    m_art_w = dst_w;
    m_art_h = dst_h;

    if (is_webp) WebPFree(decoded); else stbi_image_free(decoded);

    // Art presence changed, force scroll width to be recalculated.
    m_titleScroll  = {};
    m_artistScroll = {};
    if (auto* p = getParent()) p->invalidate();
}

void MediaTitleBar::layout(u16, u16, u16, u16) {
    setBoundaries(getX(), getY(), getWidth(), height());
}

void MediaTitleBar::resetScroll(ScrollState& s, std::string& dest, const std::string& src) {
    if (dest == src) return;
    dest = src;
    s = ScrollState{};
}

void MediaTitleBar::calcScrollWidth(tsl::gfx::Renderer* r, ScrollState& s,
                                     const std::string& text, const u32 fontSize) {
    if (s.maxW) return;
    if (m_layout == Layout::Side) {
        const s32 art_off = m_art_pixels.empty() ? 0 : kArtDim + kArtGap;
        s.maxW = static_cast<u32>(std::max(0, getWidth() - art_off));
    } else {
        // Top layout: text spans the full width below the art.
        s.maxW = static_cast<u32>(getWidth());
    }
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
    if (now - s.lastUpd < 8333333ULL) return;

    static constexpr double delay    = 3.0;
    static constexpr double pause    = 2.0;
    static constexpr double vel      = 100.0;
    static constexpr double accel    = 0.5;
    static constexpr double decel    = 0.5;
    static constexpr double invBil   = 1e-9;
    static constexpr double invAccel = 2.0;
    static constexpr double invDecel = 2.0;

    const double minDist   = s.textW;
    constexpr double accelDist = 0.5 * vel * accel;
    constexpr double decelDist = 0.5 * vel * decel;
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
                                    const s32 x, const s32 y,
                                    const s32 scissorY, const u32 scissorH,
                                    const u32 fontSize, const tsl::Color& clr) {
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
    const bool has_art = !m_art_pixels.empty();

    if (m_layout == Layout::Top) {
        if (has_art) {
            // Center art horizontally, 4 px top margin.
            const s32 ax = px + (getWidth() - m_art_w) / 2;
            const s32 ay = py + 4;
            for (s32 row = 0; row < m_art_h; ++row) {
                for (s32 col = 0; col < m_art_w; ++col) {
                    const int idx = (row * m_art_w + col) * 4;
                    renderer->setPixel(
                        static_cast<u32>(ax + col),
                        static_cast<u32>(ay + row),
                        tsl::Color(
                            static_cast<u8>(m_art_pixels[idx]     >> 4),
                            static_cast<u8>(m_art_pixels[idx + 1] >> 4),
                            static_cast<u8>(m_art_pixels[idx + 2] >> 4),
                            0xF));
                }
            }
        }

        // Text positions: below art (or at normal offsets when no art).
        const s32 art_bottom = has_art ? (4 + m_art_h + 8) : 0;
        const s32 titleY  = py + (has_art ? art_bottom + static_cast<s32>(kTitleSize)
                                           : 38);
        const s32 artistY = py + (has_art ? art_bottom + static_cast<s32>(kTitleSize)
                                           + 4 + static_cast<s32>(kArtistSize)
                                           : 74);

        if (!m_title.empty()) {
            calcScrollWidth(renderer, m_titleScroll, m_title, kTitleSize);
            drawScrollText(renderer, m_titleScroll, m_title,
                           px, titleY,
                           titleY - static_cast<s32>(kTitleSize) - 4, kTitleSize + 16,
                           kTitleSize, kWhite);
        }
        if (!m_artist.empty()) {
            calcScrollWidth(renderer, m_artistScroll, m_artist, kArtistSize);
            drawScrollText(renderer, m_artistScroll, m_artist,
                           px, artistY,
                           artistY - static_cast<s32>(kArtistSize) - 4, kArtistSize + 16,
                           kArtistSize, kDim);
        }

    } else {
        if (has_art) {
            const s32 ax = px;
            const s32 ay = py + (kHeightSide - kArtDim) / 2;
            for (s32 row = 0; row < m_art_h; ++row) {
                for (s32 col = 0; col < m_art_w; ++col) {
                    const int idx = (row * m_art_w + col) * 4;
                    renderer->setPixel(
                        static_cast<u32>(ax + col),
                        static_cast<u32>(ay + row),
                        tsl::Color(
                            static_cast<u8>(m_art_pixels[idx]     >> 4),
                            static_cast<u8>(m_art_pixels[idx + 1] >> 4),
                            static_cast<u8>(m_art_pixels[idx + 2] >> 4),
                            0xF));
                }
            }
        }

        const s32 text_x = px + (has_art ? kArtDim + kArtGap : 0);

        if (!m_title.empty()) {
            const s32 titleY = py + 38;
            calcScrollWidth(renderer, m_titleScroll, m_title, kTitleSize);
            drawScrollText(renderer, m_titleScroll, m_title,
                           text_x, titleY,
                           titleY - static_cast<s32>(kTitleSize) - 4, kTitleSize + 16,
                           kTitleSize, kWhite);
        }
        if (!m_artist.empty()) {
            const s32 artistY = py + 74;
            calcScrollWidth(renderer, m_artistScroll, m_artist, kArtistSize);
            drawScrollText(renderer, m_artistScroll, m_artist,
                           text_x, artistY,
                           artistY - static_cast<s32>(kArtistSize) - 4, kArtistSize + 16,
                           kArtistSize, kDim);
        }
    }
}
