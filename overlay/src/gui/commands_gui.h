#pragma once

#include <tesla.hpp>
#include <string>

class CommandsGui : public tsl::Gui {
public:
    CommandsGui(std::string device_id, std::string device_name);
    tsl::elm::Element* createUI() override;

private:
    std::string device_id_;
    std::string device_name_;
};
