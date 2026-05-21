#pragma once
#include "plugin.h"
#include <mutex>
#include <queue>
#include <string>

class SharePlugin : public Plugin {
public:
    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    bool on_packet_received(const NetworkPacket& np) override;

    // Pop and open one pending URL in the system browser. Returns true if a URL was opened.
    // Must be called from the main thread.
    static bool open_pending_url();

    bool send_screenshot() const;

private:
    static std::vector<unsigned char> capture_screenshot_to_buffer();

    static std::mutex s_url_mutex_;
    static std::mutex s_screenshot_mutex_;
    static std::queue<std::string> s_pending_urls_;
};
