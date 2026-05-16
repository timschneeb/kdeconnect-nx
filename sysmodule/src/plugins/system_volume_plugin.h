#pragma once
#include "plugin.h"

class SystemVolumePlugin : public Plugin {
public:
    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;

    void send_sink_list() const;

private:
    int volume_ = 50;
    bool muted_ = false;
};
