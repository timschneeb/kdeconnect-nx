#pragma once
#include "plugin.h"
#include <string>
#include <unordered_map>

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
    void post_notification(const std::string& app_id,
                           const std::string& title,
                           const std::string& body,
                           const std::string& id);
    static void write_app_icon(const std::string& icon_hash,
                               const std::vector<uint8_t>& png_data);

    // appName -> last known payloadHash, to work around Android not resending
    // payloadHash for the same icon within a notification's lifetime.
    std::unordered_map<std::string, std::string> m_app_icon_hash;

    // id -> {has_icon, time}: used to debounce duplicate packets.
    struct PostedEntry { bool has_icon; std::string time; };
    std::unordered_map<std::string, PostedEntry> m_posted_ids;
};
