#include "mpris_plugin.h"
#include "../utils/logger.h"

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
            if (!players.empty() && current_player_.empty()) {
                current_player_ = players.front();
                first_new_player = players.front();
            }
        }

        std::string list;
        for (const auto& p : players) list += " '" + p + "'";
        Logger::info("[MPRIS] Players:" + (list.empty() ? " (none)" : list));

        if (!first_new_player.empty()) {
            request_status(first_new_player);
        }
        return true;
    }

    if (np.body.contains("player") && np.body["player"].is_string()) {
        std::string player = np.body["player"].get<std::string>();
        {
            std::lock_guard lock(mutex_);
            current_player_ = player;
            if (np.body.contains("isPlaying"))    state_.is_playing    = np.body["isPlaying"].get<bool>();
            if (np.body.contains("canPause"))     state_.can_pause     = np.body["canPause"].get<bool>();
            if (np.body.contains("canPlay"))      state_.can_play      = np.body["canPlay"].get<bool>();
            if (np.body.contains("canGoNext"))    state_.can_go_next   = np.body["canGoNext"].get<bool>();
            if (np.body.contains("canGoPrevious"))state_.can_go_previous = np.body["canGoPrevious"].get<bool>();
            if (np.body.contains("title")  && np.body["title"].is_string())  state_.title  = np.body["title"].get<std::string>();
            if (np.body.contains("artist") && np.body["artist"].is_string()) state_.artist = np.body["artist"].get<std::string>();
            if (np.body.contains("album")  && np.body["album"].is_string())  state_.album  = np.body["album"].get<std::string>();
            if (np.body.contains("volume") && np.body["volume"].is_number()) state_.volume = np.body["volume"].get<int>();
        }

        std::string who = state_.artist.empty() ? state_.title : state_.artist + " - " + state_.title;
        Logger::info("[MPRIS] '" + player + "' " + (state_.is_playing ? "[playing]" : "[paused]") + " " + who);
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
    return state_;
}
