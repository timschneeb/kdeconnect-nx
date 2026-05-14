#include "share_plugin.h"
#include "../utils/logger.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

std::mutex SharePlugin::s_url_mutex_;
std::queue<std::string> SharePlugin::s_pending_urls_;

std::string SharePlugin::name() const { return "Share Plugin"; }
std::string SharePlugin::description() const { return "Receives shared URLs, text and files."; }

std::vector<std::string> SharePlugin::supported_packet_types() const {
    return { PacketTypes::ShareRequest };
}

std::vector<std::string> SharePlugin::outgoing_packet_types() const {
    return {};
}

bool SharePlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::ShareRequest) return false;

    if (np.body.contains("url") && np.body["url"].is_string()) {
        const std::string url = np.body["url"].get<std::string>();
        Logger::info("[SHARE] URL: " + url);
        std::lock_guard lock(s_url_mutex_);
        if (s_pending_urls_.size() > 1) s_pending_urls_.pop();
        s_pending_urls_.push(url);
        return true;
    }
    if (np.body.contains("text") && np.body["text"].is_string()) {
        Logger::info("[SHARE] Text: " + np.body["text"].get<std::string>());
        return true;
    }
    if (np.body.contains("filename") && np.body["filename"].is_string()) {
        Logger::info("[SHARE] File: " + np.body["filename"].get<std::string>() + " (not supported)");
        return true;
    }
    return false;
}

bool SharePlugin::open_pending_url() {
    std::string url;
    {
        std::lock_guard lock(s_url_mutex_);
        if (s_pending_urls_.empty()) return false;
        url = std::move(s_pending_urls_.front());
        s_pending_urls_.pop();
    }

#ifdef __SWITCH__
    WebCommonConfig config{};
    webPageCreate(&config, url.c_str());
    webConfigSetJsExtension(&config, true);
    webConfigSetPageCache(&config, true);
    webConfigSetBootLoadingIcon(&config, true);
    webConfigSetWhitelist(&config, ".*");
    webConfigSetScreenShot(&config, true);
    webConfigSetPointer(&config, true);
    webConfigSetMediaAutoPlay(&config, true);
    webConfigSetDisplayUrlKind(&config, true);
    webConfigSetMediaPlayerAutoClose(&config, false);
    webConfigSetPageScrollIndicator(&config, true);
    webConfigSetFooterFixedKind(&config, WebFooterFixedKind_Default);
    webConfigSetTransferMemory(&config, true);
    webConfigSetTouchEnabledOnContents(&config, true);
    webConfigSetWebAudio(&config, true);

    WebCommonReply ret{};
    webConfigShow(&config, &ret);
#else
    Logger::info("[SHARE] Would open URL: " + url);
#endif

    return true;
}
