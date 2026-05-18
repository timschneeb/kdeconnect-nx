#pragma once
#include "plugin.h"
#include <atomic>
#include <cstdint>
#include <thread>

class BatteryPlugin : public Plugin {
public:
    ~BatteryPlugin();

    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_create() override;
    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;

    void send_status() const;
    void read_remote_state(int8_t& charge, bool& charging) const;

private:
    void poll_loop();
    bool read_hardware(int32_t& charge, bool& charging) const;

    std::atomic<int32_t> cached_charge_{100};
    std::atomic<bool> cached_charging_{false};
    std::atomic<bool> cache_valid_{false};

    std::atomic<int32_t> cached_remote_charge_{-1};
    std::atomic<bool> cached_remote_charging_{false};

    std::atomic<bool> running_{false};
    std::thread poll_thread_;

#ifdef __SWITCH__
    bool psm_initialized_ = false;
#endif
};
