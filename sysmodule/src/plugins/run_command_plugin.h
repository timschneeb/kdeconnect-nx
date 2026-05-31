#pragma once
#include "plugin.h"
#include <mutex>
#include <string>
#include <unordered_map>

class RunCommandPlugin : public Plugin {
public:
    std::string name() const override;
    std::string description() const override;
    std::vector<std::string> supported_packet_types() const override;
    std::vector<std::string> outgoing_packet_types() const override;

    void on_connected(bool paired) override;
    bool on_packet_received(const NetworkPacket& np) override;

    std::vector<std::pair<std::string, std::string>> remote_command_list() const;
    void run_remote_command(const std::string& key) const;

private:
    mutable std::mutex remote_commands_mutex_;
    std::unordered_map<std::string, std::string> remote_commands_; // id -> name

    void send_local_command_list() const;
    void request_remote_command_list() const;

    static void run_local_command(const std::string& key);
};
