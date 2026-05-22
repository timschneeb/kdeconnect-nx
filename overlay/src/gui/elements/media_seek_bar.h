#pragma once
#include <tesla.hpp>

class MediaSeekBar final : public tsl::elm::TrackBar {
public:
    explicit MediaSeekBar(std::string device_id);

    // Called each IPC poll. Position is already live-computed by the sysmodule.
    void setPositionMs(int64_t pos_ms, int64_t len_ms);
    void setSeekable(bool seekable) { m_seekable = seekable; }

    void draw(tsl::gfx::Renderer* renderer) override;

private:
    std::string m_device_id;
    int64_t     m_pos_ms  = 0;
    int64_t     m_len_ms  = 0;
    bool        m_seekable = true;

    static std::string fmtTime(int64_t ms);
};