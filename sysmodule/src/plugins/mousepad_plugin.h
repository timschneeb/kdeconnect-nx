#pragma once
#include "plugin.h"

class MousepadPlugin : public Plugin {
public:
    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;
    bool on_packet_received(const NetworkPacket& np) override;
    void on_connected(bool paired) override;
    void on_create() override;
    void on_destroy() override;

private:
    void send_keyboard_state() const;
    void send_echo(const NetworkPacket& np) const;
    void inject_key(const NetworkPacket& np) const;
};
