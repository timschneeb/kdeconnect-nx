#pragma once

#include <tesla.hpp>

class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                     HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
    void update() override;

private:
    uint32_t tick_ = 0;
    char subtitle_buf_[64] = {};
};
