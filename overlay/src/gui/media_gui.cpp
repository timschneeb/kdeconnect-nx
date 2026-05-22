#include "media_gui.h"
#include "gui_common.h"
#include "../utils/media_symbols.hpp"
#include <kdec/ipc_client.h>
#include <cstdio>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
static constexpr tsl::Color kTransparent{0, 0, 0, 0};
#define kWhite  tsl::gfx::Renderer::a(tsl::Color{0xF, 0xF, 0xF, 0xF})
#define kDim    tsl::gfx::Renderer::a(tsl::Color{0x9, 0x9, 0x9, 0xF})
#define kFaint  tsl::gfx::Renderer::a(tsl::Color{0x5, 0x5, 0x5, 0xF})

static constexpr int kTitleSize   = 22;
static constexpr int kArtistSize  = 18;
static constexpr int kTimeSize    = 16;
static constexpr int kScrollGap   = 40;    // blank pixels between doubled text
static constexpr u8  kScrollPause = 90;    // frames to pause before/after scroll

// ===========================================================================
// MediaIconButton
// ===========================================================================

MediaIconButton::MediaIconButton(const AlphaSymbol* icon, std::function<void()> action)
    : m_icon(icon), m_action(std::move(action))
{
    m_isItem = false;
}

tsl::elm::Element* MediaIconButton::requestFocus(tsl::elm::Element*,
                                                 tsl::FocusDirection dir) {
    return (dir == tsl::FocusDirection::None) ? this : nullptr;
}

bool MediaIconButton::onClick(u64 keys) {
    if ((keys & HidNpadButton_A) && !m_disabled && m_action) {
        m_action();
        triggerClickAnimation();
        return true;
    }
    return false;
}

void MediaIconButton::draw(tsl::gfx::Renderer* renderer) {
    const s32 cx = getX() + getWidth()  / 2;
    const s32 cy = getY() + getHeight() / 2;
    const s32 r  = std::min(getWidth(), getHeight()) / 2;

    renderer->drawCircle(cx, cy, r, false, m_disabled ? kFaint : kDim);

    if (m_icon) {
        tsl::Color iconCol = m_disabled
            ? tsl::gfx::Renderer::a(tsl::Color{0x4, 0x4, 0x4, 0xF})
            : kDim;
        m_icon->draw(cx, cy, renderer, iconCol);
    }
}

void MediaIconButton::drawFocusBackground(tsl::gfx::Renderer*) {
    if (this->m_clickAnimationProgress > 0) {
        float rem = tsl::style::ListItemHighlightLength *
            (1.0f - ((ult::nowNs() - this->m_animationStartTime) * 0.000001f) * 0.002f);
        if (rem < 0.0f) rem = 0.0f;
        this->m_clickAnimationProgress = static_cast<u8>(rem);
    }
}

void MediaIconButton::drawHighlight(tsl::gfx::Renderer* renderer) {
    if (!this->m_focused) return;

    const u64 now_ns = ult::nowNs();
    const double p = (ult::cos(2.0 * ult::_M_PI *
        std::fmod(now_ns * 0.000000001 - 0.25, 1.0)) + 1.0) * 0.5;

    tsl::s_highlightColor = {
        static_cast<u8>(tsl::highlightColor2.r +
            (tsl::highlightColor1.r - tsl::highlightColor2.r) * p + 0.5),
        static_cast<u8>(tsl::highlightColor2.g +
            (tsl::highlightColor1.g - tsl::highlightColor2.g) * p + 0.5),
        static_cast<u8>(tsl::highlightColor2.b +
            (tsl::highlightColor1.b - tsl::highlightColor2.b) * p + 0.5),
        0xF
    };

    const auto hl   = tsl::gfx::Renderer::a(tsl::s_highlightColor);
    const s32  cx   = getX() + getWidth()  / 2;
    const s32  cy   = getY() + getHeight() / 2;
    const s32  base = std::min(getWidth(), getHeight()) / 2;

    renderer->drawCircle(cx, cy, base + 5, true, hl);
}

// ===========================================================================
// MediaButtonRow
// ===========================================================================

MediaButtonRow::MediaButtonRow() {
    m_isItem = true;
    for (auto& btn : m_buttons)
        btn = new MediaIconButton(nullptr, nullptr);
}

MediaButtonRow::~MediaButtonRow() {
    for (auto* b : m_buttons) delete b;
}

void MediaButtonRow::setButton(int idx, const AlphaSymbol* icon,
                               std::function<void()> action, bool disabled) {
    if (idx < 0 || idx >= 5 || !m_buttons[idx]) return;
    m_buttons[idx]->setIcon(icon);
    m_buttons[idx]->setAction(std::move(action));
    m_buttons[idx]->setDisabled(disabled);
}

void MediaButtonRow::setButtonDisabled(int idx, bool disabled) {
    if (idx >= 0 && idx < 5 && m_buttons[idx])
        m_buttons[idx]->setDisabled(disabled);
}

void MediaButtonRow::setButtonIcon(int idx, const AlphaSymbol* icon) {
    if (idx >= 0 && idx < 5 && m_buttons[idx])
        m_buttons[idx]->setIcon(icon);
}

tsl::elm::Element* MediaButtonRow::requestFocus(tsl::elm::Element*,
                                                tsl::FocusDirection dir) {
    if (dir == tsl::FocusDirection::None ||
        dir == tsl::FocusDirection::Up   ||
        dir == tsl::FocusDirection::Down)
        return this;
    return nullptr;
}

bool MediaButtonRow::handleInput(u64 keysDown, u64, const HidTouchState&,
                                 HidAnalogStickState leftStick, HidAnalogStickState) {
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

// ===========================================================================
// MediaSeekBar
// ===========================================================================

MediaSeekBar::MediaSeekBar(std::string device_id)
    : tsl::elm::TrackBar("", false, false, true)   // V2 style → touch target at y+53
    , m_device_id(std::move(device_id))
{
    m_isItem = true;
    setValueChangedListener([this](u16 val) {
        if (m_len_ms > 0)
            kdecIpcSendMediaAction(KdecMediaAction::SetPosition,
                                   (int64_t)val * m_len_ms / 100);
    });
}

void MediaSeekBar::setPositionMs(int64_t pos_ms, int64_t len_ms) {
    m_pos_ms = pos_ms;
    m_len_ms = len_ms;
    setProgress(len_ms > 0 ? (u16)(pos_ms * 100 / len_ms) : 0);
}

std::string MediaSeekBar::fmtTime(int64_t ms) {
    const int s = (int)(ms / 1000);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

void MediaSeekBar::draw(tsl::gfx::Renderer* renderer) {
    // V2-style geometry — must match onTouch() so the touch target aligns visually
    const s32 xPos  = getX() + 59;
    const s32 yPos  = getY() + 53;
    const s32 width = getWidth() - 95;
    const s32 span  = m_maxValue - m_minValue;
    const s32 handle = (span > 0) ? (width * m_value / span) : 0;

    // Track bar (background then filled)
    drawBar(renderer, xPos, yPos - 3, (u16)width,  tsl::trackBarEmptyColor);
    if (handle > 0 && m_seekable)
        drawBar(renderer, xPos, yPos - 3, (u16)handle, tsl::trackBarFullColor);

    // Slider circle — greyed out when not seekable, highlight when focused+seekable
    if (m_focused && m_seekable) {
        renderer->drawCircle(xPos + handle, yPos, 16, true,
                             tsl::gfx::Renderer::a(tsl::s_highlightColor));
        renderer->drawCircle(xPos + handle, yPos, 12, true,
                             tsl::gfx::Renderer::a(tsl::trackBarSliderMalleableColor));
    } else {
        const auto borderCol = m_seekable ? tsl::trackBarSliderBorderColor : tsl::trackBarEmptyColor;
        const auto sliderCol = m_seekable ? tsl::trackBarSliderColor       : tsl::trackBarEmptyColor;
        renderer->drawCircle(xPos + handle, yPos, 16, true,
                             tsl::gfx::Renderer::a(borderCol));
        renderer->drawCircle(xPos + handle, yPos, 13, true,
                             tsl::gfx::Renderer::a(sliderCol));
    }

    // Time labels
    const s32 labelY = getY() + 30;
    if (m_seekable) {
        const int64_t dispPos = (m_len_ms > 0)
            ? ((int64_t)getProgress() * m_len_ms / 100)
            : 0;
        const std::string posStr = fmtTime(dispPos);
        const std::string lenStr = fmtTime(m_len_ms);

        renderer->drawString(posStr.c_str(), false, xPos, labelY, kTimeSize, kDim);
        const u32 lenW = renderer->drawString(lenStr.c_str(), false, 0, 0,
                                              kTimeSize, kTransparent).first;
        renderer->drawString(lenStr.c_str(), false, xPos + width - (s32)lenW,
                             labelY, kTimeSize, kDim);
    } else {
        renderer->drawString("--:--", false, xPos, labelY, kTimeSize, kFaint);
        const u32 liveW = renderer->drawString("LIVE", false, 0, 0,
                                               kTimeSize, kTransparent).first;
        renderer->drawString("LIVE", false, xPos + width - (s32)liveW,
                             labelY, kTimeSize, kFaint);
    }

    // Bottom separator
    renderer->drawRect(getX() + 23, getBottomBound(),
                       getWidth() + 40, 1,
                       tsl::gfx::Renderer::a(tsl::separatorColor));
}

// ===========================================================================
// MediaTitleBar
// ===========================================================================

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

// ===========================================================================
// MediaGui
// ===========================================================================

MediaGui::MediaGui(std::string device_id, std::string device_name)
    : m_device_id(std::move(device_id)), m_device_name(std::move(device_name)) {}

tsl::elm::Element* MediaGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Media Remote", m_device_name);
    auto* list  = new tsl::elm::List();

    m_title_bar = new MediaTitleBar();
    list->addItem(m_title_bar, MediaTitleBar::Height);

    m_seek_bar = new MediaSeekBar(m_device_id);
    list->addItem(m_seek_bar, tsl::style::TrackBarDefaultHeight);

    m_btn_row = new MediaButtonRow();
    m_btn_row->setButton(MediaButtonRow::IDX_REWIND,   &media_sym::backward::symbol,
        []{ kdecIpcSendMediaAction(KdecMediaAction::Seek, -10000); });
    m_btn_row->setButton(MediaButtonRow::IDX_PREV,     &media_sym::prev::symbol,
        []{ kdecIpcSendMediaAction(KdecMediaAction::Previous); });
    m_btn_row->setButton(MediaButtonRow::IDX_PLAY,     &media_sym::play::symbol,
        []{ kdecIpcSendMediaAction(KdecMediaAction::PlayPause); });
    m_btn_row->setButton(MediaButtonRow::IDX_NEXT,     &media_sym::next::symbol,
        []{ kdecIpcSendMediaAction(KdecMediaAction::Next); });
    m_btn_row->setButton(MediaButtonRow::IDX_FFORWARD, &media_sym::forward::symbol,
        []{ kdecIpcSendMediaAction(KdecMediaAction::Seek, 10000); });
    list->addItem(m_btn_row, MediaButtonRow::Height);

    frame->setContent(list);
    pollAndUpdate();
    return frame;
}

bool MediaGui::handleInput(u64 keysDown, u64, const HidTouchState&,
                           HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) { tsl::goBack(); return true; }
    return false;
}

void MediaGui::update() {
    if (++m_tick < 30) return;
    m_tick = 0;
    pollAndUpdate();
}

void MediaGui::pollAndUpdate() {
    KdecMediaInfo info{};
    if (R_FAILED(kdecIpcGetMediaInfo(info))) {
        if (m_title_bar) m_title_bar->setInfo("No media playing", "");
        return;
    }

    const std::string title  = info.title[0]  ? info.title  : "Unknown";
    const std::string artist = info.artist[0] ? info.artist : "Unknown Artist";

    if (m_title_bar) m_title_bar->setInfo(title, artist);
    if (m_seek_bar) {
        m_seek_bar->setSeekable(info.can_seek);
        m_seek_bar->setPositionMs(info.can_seek ? info.position : 0,
                                  info.can_seek ? info.length   : 0);
    }

    if (m_btn_row) {
        m_btn_row->setButtonIcon(MediaButtonRow::IDX_PLAY,
            info.is_playing ? &media_sym::pause::symbol : &media_sym::play::symbol);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_PREV,     !info.can_go_previous);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_NEXT,     !info.can_go_next);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_REWIND,   !info.can_seek);
        m_btn_row->setButtonDisabled(MediaButtonRow::IDX_FFORWARD, !info.can_seek);
    }
}
