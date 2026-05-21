#pragma once
#include "plugin.h"
#include <mutex>

class SystemVolumePlugin : public Plugin {
public:
    struct SinkState {
        std::string name;
        std::string description;
        int         volume;           // normalized 0–100
        int         max_volume;       // raw protocol max (for denormalization on send)
        bool        is_muted;
        bool        is_default_output;
    };

    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;

    void send_sink_list() const;
    std::vector<SinkState> get_remote_sink_list() const;
    void set_remote_sink(const std::string& sink_name, int volume, bool muted, bool is_default_output);

private:
    // Local (Switch) state
    int  volume_ = 50;
    bool muted_  = false;

    // Remote sink cache (written by network thread, read by IPC thread)
    mutable std::mutex     remote_sinks_mutex_;
    std::vector<SinkState> remote_sinks_;
};