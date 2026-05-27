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
    return { PacketTypes::SystemVolumeRequest, PacketTypes::SystemVolume };
}

std::vector<std::string> SystemVolumePlugin::outgoing_packet_types() const {
    return { PacketTypes::SystemVolume, PacketTypes::SystemVolumeRequest };
}

void SystemVolumePlugin::on_connected(bool paired) {
    if (!paired) return;
    send_sink_list();
    // Ask the remote device for its sink list
    NetworkPacket req;
    req.type = PacketTypes::SystemVolumeRequest;
    req.body = { {"requestSinks", true} };
    send_packet(req);
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
    // Remote is requesting our local sinks or adjusting our local volume
    if (np.type == PacketTypes::SystemVolumeRequest) {
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

        Logger::info("%d%%%s", volume_, muted_ ? " (muted)" : "");

        NetworkPacket pkt;
        pkt.type = PacketTypes::SystemVolume;
        pkt.body = {
            {"name",    kSinkName},
            {"volume",  volume_},
            {"muted",   muted_},
            {"enabled", true}
        };
        send_packet(pkt);
        return true;
    }

    // Remote is advertising its own sinks (initial list or single-sink state update)
    if (np.type == PacketTypes::SystemVolume) {
        std::lock_guard<std::mutex> lock(remote_sinks_mutex_);

        if (np.body.contains("sinkList") && np.body["sinkList"].is_array()) {
            remote_sinks_.clear();
            for (const auto& s : np.body["sinkList"]) {
                SinkState state;
                state.name        = s.value("name", "");
                state.description = s.value("description", "");
                state.max_volume  = s.value("maxVolume", 100);
                int raw_vol       = s.value("volume", 0);
                state.volume      = state.max_volume > 0 ? (raw_vol * 100) / state.max_volume : 0;
                state.is_muted          = s.value("muted", false);
                state.is_default_output = s.value("enabled", false);
                remote_sinks_.push_back(std::move(state));
            }
        } else if (np.body.contains("name")) {
            const std::string n = np.body["name"].get<std::string>();
            for (auto& s : remote_sinks_) {
                if (s.name != n) continue;
                if (np.body.contains("volume") && np.body["volume"].is_number()) {
                    int raw_vol = np.body["volume"].get<int>();
                    s.volume = s.max_volume > 0 ? (raw_vol * 100) / s.max_volume : 0;
                }
                if (np.body.contains("muted") && np.body["muted"].is_boolean())
                    s.is_muted = np.body["muted"].get<bool>();
                if (np.body.contains("enabled") && np.body["enabled"].is_boolean())
                    s.is_default_output = np.body["enabled"].get<bool>();
                break;
            }
        }
        return true;
    }

    return false;
}

std::vector<SystemVolumePlugin::SinkState> SystemVolumePlugin::get_remote_sink_list() const {
    std::lock_guard<std::mutex> lock(remote_sinks_mutex_);
    return remote_sinks_;
}

void SystemVolumePlugin::set_remote_sink(const std::string& sink_name, int volume, bool muted, bool is_default_output) {
    int max_volume = 100;
    {
        std::lock_guard<std::mutex> lock(remote_sinks_mutex_);
        for (const auto& s : remote_sinks_) {
            if (s.name == sink_name) { max_volume = s.max_volume; break; }
        }
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::SystemVolumeRequest;
    pkt.body = {
        {"name",   sink_name},
        {"volume", (volume * max_volume) / 100},
        {"muted",  muted},
    };
    if (is_default_output)
        pkt.body["enabled"] = true;
    send_packet(pkt);
}

void SystemVolumePlugin::send_sink_list() const {
    int vol = get_system_volume();

    NetworkPacket pkt;
    pkt.type = PacketTypes::SystemVolume;
    pkt.body = {
        {"sinkList", nlohmann::json::array({{
            {"name",        kSinkName},
            {"description", kSinkDescription},
            {"muted",       false},
            {"volume",      vol},
            {"maxVolume",   kMaxVolume},
            {"enabled",     true}
        }})}
    };
    send_packet(pkt);
}