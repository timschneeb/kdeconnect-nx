#pragma once
#include "plugin.h"
#include <string>

class NotificationPlugin : public Plugin {
public:
    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_create() override;
    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;

    void request_active_notifications() const;

private:
    void post_notification(const std::string& title,
                           const std::string& body,
                           const std::string& id);
};
