#pragma once
#include "plugin.h"
#include <atomic>
#include <chrono>
#include <cstdint>

class BatteryPlugin : public Plugin {
public:
    ~BatteryPlugin() override;

    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_create() override;
    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;
    void process_events() override;

    void send_status() const;
    void read_remote_state(int8_t& charge, bool& charging) const;

private:
    bool read_hardware(int32_t& charge, bool& charging) const;

    // Atomics: written from main thread (process_events), read from io_thread (send_status via on_packet_received)
    std::atomic<int32_t> cached_charge_{100};
    std::atomic<bool> cached_charging_{false};
    std::atomic<bool> cache_valid_{false};

    std::atomic<int32_t> cached_remote_charge_{-1};
    std::atomic<bool> cached_remote_charging_{false};

    bool active_ = false;
    bool notified_low_ = false;
    std::chrono::steady_clock::time_point last_poll_{};

#ifdef __SWITCH__
    bool psm_initialized_ = false;
#endif
};
