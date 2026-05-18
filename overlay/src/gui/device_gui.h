#pragma once

#include <tesla.hpp>
#include <kdec/ipc.h>

class DeviceGui : public tsl::Gui {
public:
    explicit DeviceGui(KdecDeviceInfo dev);
    tsl::elm::Element* createUI() override;

private:
    KdecDeviceInfo dev_;
};
