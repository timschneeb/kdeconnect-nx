#include "system_volume_plugin.h"
#include "../utils/logger.h"

static constexpr const char* kSinkName = "output";
static constexpr const char* kSinkDescription = "System Audio";
static constexpr int kMaxVolume = 100;

std::string SystemVolumePlugin::name() const { return "System Volume Plugin"; }
std::string SystemVolumePlugin::description() const { return "Exposes and controls system audio volume."; }

std::vector<std::string> SystemVolumePlugin::supported_packet_types() const {
    return { PacketTypes::SystemVolumeRequest };
}

std::vector<std::string> SystemVolumePlugin::outgoing_packet_types() const {
    return { PacketTypes::SystemVolume };
}

void SystemVolumePlugin::on_connected(bool paired) {
    if (paired) {
        send_sink_list();
    }
}

bool SystemVolumePlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::SystemVolumeRequest) return false;

    if (np.body.value("requestSinks", false)) {
        send_sink_list();
        return true;
    }

    if (!np.body.contains("name")) return false;

    if (np.body.contains("volume") && np.body["volume"].is_number())
        volume_ = np.body["volume"].get<int>();
    if (np.body.contains("muted") && np.body["muted"].is_boolean())
        muted_ = np.body["muted"].get<bool>();

    Logger::info("[VOLUME] " + std::to_string(volume_) + "%" + (muted_ ? " (muted)" : ""));

    NetworkPacket pkt;
    pkt.type = PacketTypes::SystemVolume;
    pkt.body = {
        {"name", kSinkName},
        {"volume", volume_},
        {"muted", muted_},
        {"enabled", true}
    };
    send_packet(pkt);
    return true;
}

void SystemVolumePlugin::send_sink_list() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::SystemVolume;
    pkt.body = {
        {"sinkList", nlohmann::json::array({{
            {"name", kSinkName},
            {"description", kSinkDescription},
            {"muted", muted_},
            {"volume", volume_},
            {"maxVolume", kMaxVolume},
            {"enabled", true}
        }})}
    };
    send_packet(pkt);
}
