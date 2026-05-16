#pragma once
#include "plugin.h"
#include <mutex>
#include <vector>

class MprisPlugin : public Plugin {
public:
    struct PlayerState {
        bool can_pause = false;
        bool can_play = false;
        bool can_go_next = false;
        bool can_go_previous = false;
        bool is_playing = false;
        std::string title;
        std::string artist;
        std::string album;
        int volume = 0;
    };

    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;

    void request_player_list() const;
    void request_status(const std::string& player) const;
    void send_action(const std::string& player, const std::string& action) const;
    void set_volume(const std::string& player, int volume) const;

    std::vector<std::string> player_list() const;
    std::string current_player() const;
    PlayerState player_state() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> player_list_;
    std::string current_player_;
    PlayerState state_;
};
