#pragma once

#include <tesla.hpp>
#include <kdec/ipc.h>

class DeviceGui : public tsl::Gui {
public:
    explicit DeviceGui(const KdecDeviceInfo &dev);
    tsl::elm::Element* createUI() override;
    void update() override;

private:
    KdecDeviceInfo dev_;
    int screenshot_ticks_ = 0;
};
