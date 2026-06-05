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
    return { PacketTypes::Mpris, PacketTypes::MprisRequest };
}

std::vector<std::string> MprisPlugin::outgoing_packet_types() const {
    return { PacketTypes::MprisRequest, PacketTypes::Mpris };
}

void MprisPlugin::on_connected(const bool paired) {
    if (paired) {
        request_player_list();
    }
}

bool MprisPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::Mpris) return false;

    if (np.body.has("playerList")) {
        std::vector<std::string> players;
        np.body.each_str("playerList", [&](const char* s) { players.emplace_back(s); });

        std::string first_new_player;
        {
            std::lock_guard lock(mutex_);
            player_list_ = players;
            // Always track the most recently advertised player (players.front())
            if (!players.empty() && players.front() != current_player_) {
                current_player_     = players.front();
                first_new_player    = players.front();
                state_              = PlayerState{};
                seek_lock_until_ms_ = 0;
            }
        }

        std::string list;
        for (const auto& p : players) list += " '" + p + "'";
        Logger::info("Players:%s", list.empty() ? " (none)" : list.c_str());

        if (!first_new_player.empty()) {
            request_status(first_new_player);
        }
        return true;
    }

    if (np.body.is_str("player")) {
        std::string player = np.body.get_str("player");
        {
            std::lock_guard lock(mutex_);
            // Ignore status updates from players other than the selected one.
            // Firefox exposes two MPRIS players ("Firefox" and "Mozilla firefox");
            // packets from the non-selected one corrupt state with pos:0 resets.
            if (!current_player_.empty() && player != current_player_) return true;
            if (np.body.has("isPlaying"))     state_.is_playing      = np.body.get_bool("isPlaying");
            if (np.body.has("canPause"))      state_.can_pause       = np.body.get_bool("canPause");
            if (np.body.has("canPlay"))       state_.can_play        = np.body.get_bool("canPlay");
            if (np.body.has("canGoNext"))     state_.can_go_next     = np.body.get_bool("canGoNext");
            if (np.body.has("canGoPrevious")) state_.can_go_previous = np.body.get_bool("canGoPrevious");
            if (np.body.has("canSeek"))       state_.can_seek        = np.body.get_bool("canSeek");
            if (np.body.is_str("title"))      state_.title  = np.body.get_str("title");
            if (np.body.is_str("artist"))     state_.artist = np.body.get_str("artist");
            if (np.body.is_str("album"))      state_.album  = np.body.get_str("album");
            if (np.body.is_num("volume"))     state_.volume = np.body.get_int("volume");
            // Protocol packets are incremental, only update fields that are present.
            // pos and length use -1 as a sentinel for "unknown/seeking"; ignore those
            // so a transient partial update doesn't corrupt previously good values.
            // Also skip pos:0 when the same packet carries length:-1 (Firefox's raw MPRIS)
            // emits {pos:0, length:-1} during buffering/transition which is not a real position.
            if (np.body.is_num("pos") && now_ms() >= seek_lock_until_ms_) {
                const int64_t p = np.body.get_i64("pos");
                const bool co_length_bad = np.body.is_num("length") && np.body.get_i64("length") < 0;
                if (p > 0 || !co_length_bad) {
                    if (p >= 0) {
                        state_.position              = p;
                        state_.last_position_time_ms = now_ms();
                    }
                }
            }
            if (np.body.is_num("length")) {
                const int64_t l = np.body.get_i64("length");
                if (l >= 0) state_.length = l;
            }
        }

        std::string who = state_.artist.empty() ? state_.title : state_.artist + " - " + state_.title;
        Logger::info("'%s' %s %s", player.c_str(),
                 state_.is_playing ? "[playing]" : "[paused]",
                 who.c_str());
        return true;
    }

    return false;
}

void MprisPlugin::request_player_list() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body.set("requestPlayerList", true);
    send_packet(pkt);
}

void MprisPlugin::request_status(const std::string& player) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body.set("player",           player)
            .set("requestNowPlaying", true)
            .set("requestVolume",     true);
    send_packet(pkt);
}

void MprisPlugin::send_action(const std::string& player, const std::string& action) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body.set("player", player).set("action", action);
    send_packet(pkt);
}

void MprisPlugin::set_volume(const std::string& player, const int volume) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body.set("player", player).set("setVolume", volume);
    send_packet(pkt);
}

void MprisPlugin::seek(const std::string& player, const int64_t offset_ms) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body.set("player", player).set("Seek", offset_ms * 1000);
    send_packet(pkt);
}

void MprisPlugin::set_position(const std::string& player, const int64_t position_ms) {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MprisRequest;
    pkt.body.set("player", player).set("SetPosition", position_ms);
    send_packet(pkt);
    // Mirror Android's sendSetPosition(): immediately anchor the local position so
    // subsequent player_state() calls advance from the new seek point.
    std::lock_guard lock(mutex_);
    state_.position              = position_ms;
    state_.last_position_time_ms = now_ms();
    seek_lock_until_ms_          = now_ms() + 500;
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
