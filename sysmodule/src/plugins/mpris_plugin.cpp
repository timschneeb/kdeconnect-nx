#include "mpris_plugin.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>

int64_t MprisPlugin::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string MprisPlugin::name() const { return "MPRIS Plugin"; }
std::string MprisPlugin::description() const { return "Remote control for media players on other devices."; }

std::vector<std::string> MprisPlugin::supported_packet_types() const {
    return { PacketTypes::Mpris };
}

std::vector<std::string> MprisPlugin::outgoing_packet_types() const {
    return { PacketTypes::MprisRequest };
}

void MprisPlugin::on_connected(bool paired) {
    if (paired) {
        request_player_list();
    }
}

bool MprisPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::Mpris) return false;

    if (np.body.contains("playerList")) {
        std::vector<std::string> players;
        for (const auto& p : np.body["playerList"]) {
            if (p.is_string()) players.push_back(p.get<std::string>());
        }

        std::string first_new_player;
        {
            std::lock_guard lock(mutex_);
            player_list_ = players;
            // Always track the most recently advertised player (players.front())
            if (!players.empty() && players.front() != current_player_) {
                current_player_     = players.front();
                first_new_player    = players.front();
                state_              = PlayerState{};
            }
        }

        std::string list;
        for (const auto& p : players) list += " '" + p + "'";
        Logger::info("Players:" + (list.empty() ? " (none)" : list));

        if (!first_new_player.empty()) {
            request_status(first_new_player);
        }
        return true;
    }

    if (np.body.contains("player") && np.body["player"].is_string()) {
        std::string player = np.body["player"].get<std::string>();
        {
            std::lock_guard lock(mutex_);
            // Ignore status updates from players other than the selected one.
            // Firefox exposes two MPRIS players ("Firefox" and "Mozilla firefox");
            // packets from the non-selected one corrupt state with pos:0 resets.
            if (!current_player_.empty() && player != current_player_) return true;
            if (np.body.contains("isPlaying"))    state_.is_playing    = np.body["isPlaying"].get<bool>();
            if (np.body.contains("canPause"))     state_.can_pause     = np.body["canPause"].get<bool>();
            if (np.body.contains("canPlay"))      state_.can_play      = np.body["canPlay"].get<bool>();
            if (np.body.contains("canGoNext"))    state_.can_go_next   = np.body["canGoNext"].get<bool>();
            if (np.body.contains("canGoPrevious"))state_.can_go_previous = np.body["canGoPrevious"].get<bool>();
            if (np.body.contains("canSeek"))      state_.can_seek        = np.body["canSeek"].get<bool>();
            if (np.body.contains("title")  && np.body["title"].is_string())  state_.title  = np.body["title"].get<std::string>();
            if (np.body.contains("artist") && np.body["artist"].is_string()) state_.artist = np.body["artist"].get<std::string>();
            if (np.body.contains("album")  && np.body["album"].is_string())  state_.album  = np.body["album"].get<std::string>();
            if (np.body.contains("volume") && np.body["volume"].is_number()) state_.volume = np.body["volume"].get<int>();
            // Protocol packets are incremental, only update fields that are present.
            // pos and length use -1 as a sentinel for "unknown/seeking"; ignore those
            // so a transient partial update doesn't corrupt previously good values.
            // Also skip pos:0 when the same packet carries length:-1 (Firefox's raw MPRIS)
            // emits {pos:0, length:-1} during buffering/transition which is not a real position.
            if (np.body.contains("pos") && np.body["pos"].is_number()) {
                const int64_t p = np.body["pos"].get<int64_t>();
                const bool co_length_bad = np.body.contains("length")
                    && np.body["length"].is_number()
                    && np.body["length"].get<int64_t>() < 0;
                if (p > 0 || !co_length_bad) {
                    if (p >= 0) {
                        state_.position              = p;
                        state_.last_position_time_ms = now_ms();
                    }
                }
            }
            if (np.body.contains("length") && np.body["length"].is_number()) {
                const int64_t l = np.body["length"].get<int64_t>();
                if (l >= 0) state_.length = l;
            }
        }

        std::string who = state_.artist.empty() ? state_.title : state_.artist + " - " + state_.title;
        Logger::info("'" + player + "' " + (state_.is_playing ? "[playing]" : "[paused]") + " " + who);
        return true;
    }

    return false;
}

void MprisPlugin::request_player_list() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body = { {"requestPlayerList", true} };
    send_packet(pkt);
}

void MprisPlugin::request_status(const std::string& player) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body = {
        {"player", player},
        {"requestNowPlaying", true},
        {"requestVolume", true}
    };
    send_packet(pkt);
}

void MprisPlugin::send_action(const std::string& player, const std::string& action) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body = { {"player", player}, {"action", action} };
    send_packet(pkt);
}

void MprisPlugin::set_volume(const std::string& player, int volume) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body = { {"player", player}, {"setVolume", volume} };
    send_packet(pkt);
}

void MprisPlugin::seek(const std::string& player, int64_t offset_ms) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body = { {"player", player}, {"Seek", offset_ms} };
    send_packet(pkt);
}

void MprisPlugin::set_position(const std::string& player, int64_t position_ms) {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body = { {"player", player}, {"SetPosition", position_ms} };
    send_packet(pkt);
    // Mirror Android's sendSetPosition(): immediately anchor the local position so
    // subsequent player_state() calls advance from the new seek point.
    std::lock_guard lock(mutex_);
    state_.position              = position_ms;
    state_.last_position_time_ms = now_ms();
}

std::vector<std::string> MprisPlugin::player_list() const {
    std::lock_guard lock(mutex_);
    return player_list_;
}

std::string MprisPlugin::current_player() const {
    std::lock_guard lock(mutex_);
    return current_player_;
}

MprisPlugin::PlayerState MprisPlugin::player_state() const {
    std::lock_guard lock(mutex_);
    PlayerState s = state_;
    // Advance position locally, exactly as Android's MprisPlayer.position getter does:
    //   position = if (isPlaying) lastPosition + (currentTime - lastPositionTime) else lastPosition
    if (s.is_playing && s.last_position_time_ms > 0) {
        const int64_t elapsed = now_ms() - s.last_position_time_ms;
        s.position = s.position + elapsed;
        if (s.length > 0) s.position = std::min(s.position, s.length);
    }
    return s;
}
