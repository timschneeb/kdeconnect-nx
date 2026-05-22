#pragma once
#include <tesla.hpp>

class MediaSeekBar final : public tsl::elm::TrackBar {
public:
    explicit MediaSeekBar(std::string device_id);

    // Called each IPC poll. Position is already live-computed by the sysmodule.
    // Skipped while the user is dragging so the IPC poll doesn't fight user input.
    void setPositionMs(int64_t pos_ms, int64_t len_ms);
    void setSeekable(bool seekable) { m_seekable = seekable; }

    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                     HidAnalogStickState leftJoy, HidAnalogStickState rightJoy) override;
    bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY,
                 s32 prevX, s32 prevY, s32 initialX, s32 initialY) override;

    void draw(tsl::gfx::Renderer* renderer) override;

private:
    void fireSendPosition();
    static std::string fmtTime(int64_t ms);

    std::string m_device_id;
    int64_t     m_pos_ms   = 0;
    int64_t     m_len_ms   = 0;
    bool        m_seekable = true;
    bool        m_dragging = false;
};
