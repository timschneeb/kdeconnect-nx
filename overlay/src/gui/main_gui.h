#pragma once

#include <tesla.hpp>
#include <kdec/ipc.h>
#include <string>
#include <vector>

class MainGui : public tsl::Gui {
public:
    MainGui() = default;
    MainGui(std::string focusedText, std::vector<KdecDeviceInfo> devices)
        : focused_text_(std::move(focusedText)), devices_(std::move(devices)), devices_prefetched_(true) {}

    tsl::elm::Element* createUI() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                     HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
    void update() override;

private:
    bool initial_tick_ = true;
    uint32_t tick_ = 0;
    char subtitle_buf_[64] = {};
    std::string focused_text_;
    std::vector<KdecDeviceInfo> devices_;
    bool devices_prefetched_ = false;
    bool was_running_ = false;

    static bool devicesChanged(const std::vector<KdecDeviceInfo>& a, const std::vector<KdecDeviceInfo>& b);
};