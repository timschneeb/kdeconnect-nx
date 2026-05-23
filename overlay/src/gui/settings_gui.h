#pragma once

#include <tesla.hpp>

class SettingsGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                     HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
};
