#pragma once
#include "plugin.h"

class FindMyPhonePlugin : public Plugin {
public:
    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void find() const;
};
