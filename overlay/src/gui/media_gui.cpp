#include "media_gui.h"
#include "gui_common.h"
#include "../utils/media_symbols.hpp"
#include <kdec/ipc_client.h>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
static constexpr tsl::Color kTransparent{0, 0, 0, 0};
#define kWhite  tsl::gfx::Renderer::a(tsl::Color{0xF, 0xF, 0xF, 0xF})
#define kDim    tsl::gfx::Renderer::a(tsl::Color{0x9, 0x9, 0x9, 0xF})
#define kFaint  tsl::gfx::Renderer::a(tsl::Color{0x5, 0x5, 0x5, 0xF})
#define kAccent tsl::gfx::Renderer::a(tsl::Color{0xF, 0x3, 0x3, 0xF})

static constexpr int kTitleSize  = 22;
static constexpr int kArtistSize = 18;
static constexpr int kTimeSize   = 16;
static constexpr int kBtnSize    = 28;
static constexpr int kScrollGap  = 40; /* pixels of blank between doubled text */
static constexpr u8  kScrollPause = 90; /* frames to wait before/after scroll */

// ---------------------------------------------------------------------------

MediaBar::MediaBar(std::string device_id)
    : m_device_id(std::move(device_id)) {}

void MediaBar::layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) {
    this->setBoundaries(parentX, parentY, parentWidth, PreferredHeight());
}

tsl::elm::Element* MediaBar::requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection dir) {
    if (dir == tsl::FocusDirection::Left  && m_btn > 0) { m_btn--; return this; }
    if (dir == tsl::FocusDirection::Right && m_btn < 4) { m_btn++; return this; }
    if (dir == tsl::FocusDirection::None)               { return this; }
    return nullptr;
}

bool MediaBar::onClick(u64 keys) {
    if (keys & HidNpadButton_A) {
        sendAction(m_btn);
        m_click_anim = 12;
        m_click_btn  = m_btn;
        return true;
    }
    return false;
}

void MediaBar::sendAction(int btn) {
    switch (btn) {
        case 0: kdecIpcSendMediaAction(KdecMediaAction::Seek, -10000); break;
        case 1: kdecIpcSendMediaAction(KdecMediaAction::Previous);     break;
        case 2: kdecIpcSendMediaAction(KdecMediaAction::PlayPause);    break;
        case 3: kdecIpcSendMediaAction(KdecMediaAction::Next);         break;
        case 4: kdecIpcSendMediaAction(KdecMediaAction::Seek,  10000); break;
    }
}

void MediaBar::pollMedia() {
    KdecMediaInfo info{};
    if (R_FAILED(kdecIpcGetMediaInfo(info))) {
        m_has_info = false;
        return;
    }
    m_has_info = true;

    std::string newTitle  = info.title[0]  ? info.title  : "Unknown";
    std::string newArtist = info.artist[0] ? info.artist : "Unknown Artist";

    if (newTitle != m_title) {
        m_title        = newTitle;
        m_title_scroll = m_title + std::string(8, ' ') + m_title;
        m_tw = 0; m_toff = 0; m_ttrunc = false; m_tctr = 0;
    }
    if (newArtist != m_artist) {
        m_artist        = newArtist;
        m_artist_scroll = m_artist + std::string(8, ' ') + m_artist;
        m_aw = 0; m_aoff = 0; m_atrunc = false; m_actr = 0;
    }

    m_pos      = info.position;
    m_len      = info.length;
    m_playing  = info.is_playing;
    m_can_prev = info.can_go_previous;
    m_can_next = info.can_go_next;
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void MediaBar::draw(tsl::gfx::Renderer* renderer) {
    const s32 x  = this->getX();
    const s32 y  = this->getY();
    const s32 w  = this->getWidth();
    const s32 H  = PreferredHeight();
    const s32 px = x + 20;
    const s32 pw = w - 40;

    const bool focused = this->m_focused;

    if (!m_has_info) {
        renderer->drawString("No media playing", false, px, y + H / 2 + 8, 20, kDim);
        return;
    }

    /* ---- Title (line 1) ---- */
    const s32 titleY = y + H * 14 / 100;
    if (m_tw == 0)
        m_tw = renderer->drawString(m_title.c_str(), false, 0, 0, kTitleSize, kTransparent).first;
    m_ttrunc = (m_tw > (u32)pw);
    if (m_ttrunc) {
        renderer->enableScissoring(px, titleY - kTitleSize - 8, pw, kTitleSize + 12);
        renderer->drawString(m_title_scroll.c_str(), false, px - (s32)m_toff, titleY, kTitleSize, kWhite);
        renderer->disableScissoring();
        /* advance scroll */
        if (m_tctr > 0) { --m_tctr; }
        else if (++m_toff >= m_tw + kScrollGap) { m_toff = 0; m_tctr = kScrollPause; }
    } else {
        renderer->drawString(m_title.c_str(), false, px, titleY, kTitleSize, kWhite);
    }

    /* ---- Artist (line 2) ---- */
    const s32 artistY = y + H * 27 / 100;
    if (m_aw == 0)
        m_aw = renderer->drawString(m_artist.c_str(), false, 0, 0, kArtistSize, kTransparent).first;
    m_atrunc = (m_aw > (u32)pw);
    if (m_atrunc) {
        renderer->enableScissoring(px, artistY - kArtistSize - 8, pw, kArtistSize + 12);
        renderer->drawString(m_artist_scroll.c_str(), false, px - (s32)m_aoff, artistY, kArtistSize, kDim);
        renderer->disableScissoring();
        if (m_actr > 0) { --m_actr; }
        else if (++m_aoff >= m_aw + kScrollGap) { m_aoff = 0; m_actr = kScrollPause; }
    } else {
        renderer->drawString(m_artist.c_str(), false, px, artistY, kArtistSize, kDim);
    }

    /* ---- Progress bar ---- */
    const s32 barY = y + H * 44 / 100;
    const s32 barH = 4;
    renderer->drawRect(px, barY, pw, barH, kFaint);
    if (m_len > 0) {
        s32 fill = (s32)((float)pw * (float)m_pos / (float)m_len);
        if (fill < 0) fill = 0;
        if (fill > pw) fill = pw;
        renderer->drawRect(px, barY, fill, barH, kAccent);
        /* thumb */
        renderer->drawCircle(px + fill, barY + barH / 2, 6, true, kAccent);
    }

    /* ---- Time labels ---- */
    const s32 timeY = y + H * 54 / 100;
    auto fmtTime = [](int64_t ms, char* buf, size_t sz) {
        int s = (int)(ms / 1000);
        snprintf(buf, sz, "%d:%02d", s / 60, s % 60);
    };
    char posStr[16], lenStr[16];
    fmtTime(m_pos, posStr, sizeof(posStr));
    fmtTime(m_len, lenStr, sizeof(lenStr));
    renderer->drawString(posStr, false, px, timeY, kTimeSize, kDim);
    u32 lenW = renderer->drawString(lenStr, false, 0, 0, kTimeSize, kTransparent).first;
    renderer->drawString(lenStr, false, px + pw - (s32)lenW, timeY, kTimeSize, kDim);

    /* ---- Buttons: Rewind | Prev | Play/Pause | Next | FastForward ---- */
    const s32 btnY = y + H - 35;
    /* Evenly space 5 buttons across the width */
    const s32 bx[5] = {
        x + w / 6,
        x + w * 2 / 6,
        x + w / 2,
        x + w * 4 / 6,
        x + w * 5 / 6,
    };
    const s32 bR[5] = { 14, 14, 20, 14, 14 };
    const AlphaSymbol* icons[5] = {
        &media_sym::backward::symbol,
        &media_sym::prev::symbol,
        m_playing ? &media_sym::pause::symbol : &media_sym::play::symbol,
        &media_sym::next::symbol,
        &media_sym::forward::symbol,
    };

    for (int i = 0; i < 5; i++) {
        bool active   = (focused && m_btn == i);
        bool clicking = (m_click_btn == i && m_click_anim > 0);
        bool disabled = (i == 1 && !m_can_prev) || (i == 3 && !m_can_next);

        auto circleCol = clicking  ? kAccent
                       : active    ? tsl::gfx::Renderer::a(tsl::highlightColor4)
                       : disabled  ? kFaint
                                   : kDim;

        renderer->drawCircle(bx[i], btnY, bR[i], false, circleCol);

        tsl::Color iconCol = disabled ? kFaint : (clicking ? kWhite : (active ? kWhite : kDim));
        icons[i]->draw(bx[i], btnY, renderer, iconCol);
    }

    if (m_click_anim > 0) --m_click_anim;
}

// ---------------------------------------------------------------------------
// MediaGui
// ---------------------------------------------------------------------------
MediaGui::MediaGui(std::string device_id, std::string device_name)
    : m_device_id(std::move(device_id)), m_device_name(std::move(device_name)) {}

tsl::elm::Element* MediaGui::createUI() {
    auto* frame = new tsl::elm::OverlayFrame("Media Remote", m_device_name);
    auto* list  = new tsl::elm::List();

    m_bar = new MediaBar(m_device_id);
    m_bar->pollMedia();
    list->addItem(m_bar, MediaBar::PreferredHeight());

    frame->setContent(list);
    return frame;
}

bool MediaGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&,
                           HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) { tsl::goBack(); return true; }
    return false;
}

void MediaGui::update() {
    if (++m_tick < 30) return;
    m_tick = 0;
    if (m_bar) m_bar->pollMedia();
}
