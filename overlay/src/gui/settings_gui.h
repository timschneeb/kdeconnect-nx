#pragma once

#include <tesla.hpp>

class SettingsGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                     HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
private:
    tsl::elm::ListItem* m_heap_item = nullptr;
    int m_update_counter = 0;
};
