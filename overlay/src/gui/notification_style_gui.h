#pragma once

#include <tesla.hpp>

class NotificationStyleGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override;
    bool handleInput(u64 keysDown, u64, const HidTouchState&,
                     HidAnalogStickState, HidAnalogStickState) override;
};
