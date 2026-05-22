#include "media_seek_bar.h"

#include "../gui_common.h"
#include "kdec/ipc_client.h"

static constexpr int kTimeSize = 16;

MediaSeekBar::MediaSeekBar(std::string device_id)
    : TrackBar("", false, false, true)
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
    // V2-style geometry - must match onTouch() so the touch target aligns visually
    const s32 xPos  = getX() + 59;
    const s32 yPos  = getY() + 53;
    const s32 width = getWidth() - 95;
    const s32 span  = m_maxValue - m_minValue;
    const s32 handle = (span > 0) ? (width * m_value / span) : 0;

    // Track bar (background then filled)
    drawBar(renderer, xPos, yPos - 3, (u16)width,  tsl::trackBarEmptyColor);
    if (handle > 0 && m_seekable)
        drawBar(renderer, xPos, yPos - 3, (u16)handle, tsl::trackBarFullColor);

    // Slider circle - greyed out when not seekable, highlight when focused+seekable
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