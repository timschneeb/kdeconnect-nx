#pragma once

#include <tesla.hpp>
#include <string>
#include <atomic>

class MediaBar final : public tsl::elm::Element {
public:
    explicit MediaBar(std::string device_id);

    tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection dir) override;
    bool onClick(u64 keys) override;
    void draw(tsl::gfx::Renderer* renderer) override;
    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override;

    void pollMedia();

    static s32 PreferredHeight() {
        return tsl::style::ListItemDefaultHeight * 4;
    }

private:
    std::string m_device_id;

    std::string m_title;
    std::string m_artist;
    int64_t     m_pos     = 0;
    int64_t     m_len     = 0;
    bool        m_playing = false;
    bool        m_can_prev = true;
    bool        m_can_next = true;
    bool        m_has_info = false;

    /* Title scroll */
    std::string m_title_scroll;
    u32  m_tw    = 0;
    u32  m_toff  = 0;
    bool m_ttrunc = false;
    u8   m_tctr  = 0;

    /* Artist scroll */
    std::string m_artist_scroll;
    u32  m_aw    = 0;
    u32  m_aoff  = 0;
    bool m_atrunc = false;
    u8   m_actr  = 0;

    /* Button focus: 0=Rewind, 1=Prev, 2=PlayPause, 3=Next, 4=FastForward */
    int m_btn = 2;

    /* Click animation */
    int m_click_anim = 0;
    int m_click_btn  = -1;

    void sendAction(int btn);
};

class MediaGui : public tsl::Gui {
public:
    MediaGui(std::string device_id, std::string device_name);

    tsl::elm::Element* createUI() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState&,
                     HidAnalogStickState, HidAnalogStickState) override;
    void update() override;

private:
    std::string m_device_id;
    std::string m_device_name;
    MediaBar*   m_bar  = nullptr;
    uint32_t    m_tick = 0;
};
