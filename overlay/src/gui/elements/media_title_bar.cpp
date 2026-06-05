#include "media_title_bar.h"

#include "../gui_common.h"
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

MediaTitleBar::MediaTitleBar() {
    m_isItem = false;
}

void MediaTitleBar::setInfo(const std::string& title, const std::string& artist) {
    resetScroll(m_titleScroll,  m_title,  title);
    resetScroll(m_artistScroll, m_artist, artist);
}

void MediaTitleBar::setAlbumArt(const std::string& hash) {
    if (hash == m_art_hash) return;
    m_art_hash = hash;
    m_art_pixels.clear();
    // Force scroll-width recalculation now that art presence changed.
    m_titleScroll  = {};
    m_artistScroll = {};

    if (hash.empty()) return;

    // Load raw image file (no extension; format detected by magic bytes below).
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

    int w = 0, h = 0;
    uint8_t* decoded = nullptr;
    bool is_webp = false;

    // Detect WebP by RIFF....WEBP magic; fall back to stb_image for everything else.
    if (buf.size() >= 12 &&
        memcmp(buf.data(), "RIFF", 4) == 0 &&
        memcmp(buf.data() + 8, "WEBP", 4) == 0) {
        is_webp = true;
        decoded = WebPDecodeRGBA(buf.data(), buf.size(), &w, &h);
    } else {
        int channels_in_file = 0;
        decoded = stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()),
                                        &w, &h, &channels_in_file, 4);
    }

    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) { if (is_webp) WebPFree(decoded); else stbi_image_free(decoded); }
        return;
    }

    // Nearest-neighbour scale to kArtDim x kArtDim and store as RGBA8.
    static constexpr size_t kPx = static_cast<size_t>(kArtDim) * kArtDim * 4;
    m_art_pixels.resize(kPx);
    for (int oy = 0; oy < kArtDim; ++oy) {
        const int sy = oy * h / kArtDim;
        for (int ox = 0; ox < kArtDim; ++ox) {
            const int sx  = ox * w / kArtDim;
            const int src = (sy * w + sx) * 4;
            const int dst = (oy * kArtDim + ox) * 4;
            m_art_pixels[dst]     = decoded[src];
            m_art_pixels[dst + 1] = decoded[src + 1];
            m_art_pixels[dst + 2] = decoded[src + 2];
            m_art_pixels[dst + 3] = decoded[src + 3];
        }
    }

    if (is_webp) WebPFree(decoded); else stbi_image_free(decoded);
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
                                     const std::string& text, const u32 fontSize) {
    if (s.maxW) return;
    const s32 art_off = m_art_pixels.empty() ? 0 : kArtDim + kArtGap;
    s.maxW = static_cast<u32>(std::max(0, getWidth() - art_off));
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
                                    const s32 x, const s32 y, const s32 scissorY, const u32 scissorH,
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

    // Album art thumbnail left-aligned, vertically centred.
    const bool has_art = !m_art_pixels.empty();
    if (has_art) {
        const s32 ax = px;
        const s32 ay = py + (Height - kArtDim) / 2;
        for (s32 row = 0; row < kArtDim; ++row) {
            for (s32 col = 0; col < kArtDim; ++col) {
                const int idx = (row * kArtDim + col) * 4;
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
