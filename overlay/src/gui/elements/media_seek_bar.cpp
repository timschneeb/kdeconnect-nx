#include "media_seek_bar.h"

#include "../gui_common.h"
#include "kdec/ipc_client.h"

static constexpr int kTimeSize = 16;

MediaSeekBar::MediaSeekBar(std::string device_id)
    : TrackBar("", false, false, true)
    , m_device_id(std::move(device_id))
{
    m_isItem = true;
}

void MediaSeekBar::setPositionMs(int64_t pos_ms, int64_t len_ms) {
    m_len_ms = len_ms;
    if (!m_dragging) {
        m_pos_ms = pos_ms;
        setProgress(len_ms > 0 ? (u16)(pos_ms * 100 / len_ms) : 0);
    }
}

bool MediaSeekBar::handleInput(u64 keysDown, u64 keysHeld,
                                const HidTouchState& touchPos,
                                HidAnalogStickState leftJoy,
                                HidAnalogStickState rightJoy) {
    if (keysHeld & (KEY_LEFT | KEY_RIGHT))
        m_dragging = true;

    bool result = TrackBar::handleInput(keysDown, keysHeld, touchPos, leftJoy, rightJoy);

    if (m_dragging && !(keysHeld & (KEY_LEFT | KEY_RIGHT))) {
        fireSendPosition();
        m_dragging = false;
    }
    return result;
}

bool MediaSeekBar::onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY,
                            s32 prevX, s32 prevY, s32 initialX, s32 initialY) {
    if (event == tsl::elm::TouchEvent::Touch || event == tsl::elm::TouchEvent::Hold)
        m_dragging = true;

    bool result = TrackBar::onTouch(event, currX, currY, prevX, prevY, initialX, initialY);

    if (event == tsl::elm::TouchEvent::Release) {
        fireSendPosition();
        m_dragging = false;
    }
    return result;
}

void MediaSeekBar::fireSendPosition() {
    if (m_seekable && m_len_ms > 0)
        kdecIpcSendMediaAction(KdecMediaAction::SetPosition,
                               (int64_t)getProgress() * m_len_ms / 100);
}

std::string MediaSeekBar::fmtTime(int64_t ms) {
    const int s = (int)(ms / 1000);
    const int h = s / 3600;
    char buf[16];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, (s % 3600) / 60, s % 60);
    else
        snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

void MediaSeekBar::draw(tsl::gfx::Renderer* renderer) {
    // V2-style geometry - must match onTouch() so the touch target aligns visually
    const s32 xPos  = getX() + 59;
    const s32 yPos  = getY() + 53;
    const s32 width = getWidth() - 95;

    // While dragging, derive position from the TrackBar's own progress value
    const int64_t displayPos = m_dragging && m_len_ms > 0
        ? (int64_t)getProgress() * m_len_ms / 100
        : m_pos_ms;

    const s32 handle = (m_seekable && m_len_ms > 0)
        ? (s32)(width * displayPos / m_len_ms)
        : 0;

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
        const std::string posStr = fmtTime(displayPos);
        const std::string lenStr = fmtTime(m_len_ms);

        renderer->drawString(posStr.c_str(), false, xPos, labelY, kTimeSize, kDim);
        const u32 lenW = renderer->drawString(lenStr.c_str(), false, 0, 0,
                                              kTimeSize, kTransparent).first;
        renderer->drawString(lenStr.c_str(), false, xPos + width - (s32)lenW,
                             labelY, kTimeSize, kDim);
    } else {
        renderer->drawString("--:--", false, xPos, labelY, kTimeSize, kFaint);
        const u32 liveW = renderer->drawString("--:--", false, 0, 0,
                                               kTimeSize, kTransparent).first;
        renderer->drawString("--:--", false, xPos + width - (s32)liveW,
                             labelY, kTimeSize, kFaint);
    }

    // Bottom separator
    renderer->drawRect(getX() + 23, getBottomBound(),
                       getWidth() + 40, 1,
                       tsl::gfx::Renderer::a(tsl::separatorColor));
}
