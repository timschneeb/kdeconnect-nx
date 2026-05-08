#include "share_plugin.h"
#include "../utils/logger.h"

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
        Logger::info("[SHARE] URL: " + np.body["url"].get<std::string>());
        return true;
    }
    if (np.body.contains("text") && np.body["text"].is_string()) {
        Logger::info("[SHARE] Text: " + np.body["text"].get<std::string>());
        return true;
    }
    if (np.body.contains("filename") && np.body["filename"].is_string()) {
        Logger::info("[SHARE] File: " + np.body["filename"].get<std::string>() + " (not saving on PC prototype)");
        return true;
    }
    return false;
}
