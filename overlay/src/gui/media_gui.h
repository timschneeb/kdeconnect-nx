#pragma once

#include <tesla.hpp>
#include <string>

class MediaTitleBar;
class MediaSeekBar;
class MediaButtonRow;

class MediaGui : public tsl::Gui {
public:
    MediaGui(std::string device_id, std::string device_name);

    tsl::elm::Element* createUI() override;
    bool               handleInput(u64 keysDown, u64, const HidTouchState&,
                                   HidAnalogStickState, HidAnalogStickState) override;
    void               update() override;

private:
    std::string m_device_id;
    std::string m_device_name;

    MediaTitleBar*  m_title_bar = nullptr;
    MediaSeekBar*   m_seek_bar  = nullptr;
    MediaButtonRow* m_btn_row   = nullptr;

    uint32_t m_tick     = 0;
    bool     m_show_art = true;

    void pollAndUpdate() const;
};
