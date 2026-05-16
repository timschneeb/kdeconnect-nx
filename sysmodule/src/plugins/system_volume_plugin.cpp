#include "system_volume_plugin.h"
#include "utils/logger.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

static constexpr const char* kSinkName = "Output";
static constexpr const char* kSinkDescription = "Master Audio Output";
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

static int get_system_volume() {
    int vol = 50;
#ifdef __SWITCH__
    if (R_SUCCEEDED(audctlInitialize())) {
        float vol_f = 0.5f;
        if (R_SUCCEEDED(audctlGetSystemOutputMasterVolume(&vol_f)))
            vol = static_cast<int>(vol_f * 100.0f);
        audctlExit();
    }
#endif
    return vol;
}

static void set_system_volume(int volume) {
#ifdef __SWITCH__
    if (R_SUCCEEDED(audctlInitialize())) {
        audctlSetSystemOutputMasterVolume(volume / 100.0f);
        audctlExit();
    }
#endif
}

bool SystemVolumePlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::SystemVolumeRequest) return false;

    if (np.body.value("requestSinks", false)) {
        send_sink_list();
        return true;
    }

    if (!np.body.contains("name")) return false;

    if (np.body.contains("volume") && np.body["volume"].is_number()) {
        volume_ = np.body["volume"].get<int>();
        set_system_volume(volume_);
    }
    if (np.body.contains("muted") && np.body["muted"].is_boolean()) {
        muted_ = np.body["muted"].get<bool>();
        // mute not exposed via audctl; lower to 0 when muted
        if (muted_) set_system_volume(0);
        else         set_system_volume(volume_);
    }

    Logger::info(std::to_string(volume_) + "%" + (muted_ ? " (muted)" : ""));

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
    int vol = get_system_volume();

    NetworkPacket pkt;
    pkt.type = PacketTypes::SystemVolume;
    pkt.body = {
        {"sinkList", nlohmann::json::array({{
            {"name", kSinkName},
            {"description", kSinkDescription},
            {"muted", false},
            {"volume", vol},
            {"maxVolume", kMaxVolume},
            {"enabled", true}
        }})}
    };
    send_packet(pkt);
}
