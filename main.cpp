#include <iostream>
#include <sstream>
#include <string>

#include "kdeconnect_client.h"
#include "src/utils/logger.h"
#include "plugins/ping_plugin.h"
#include "plugins/find_my_phone_plugin.h"
#include "plugins/mpris_plugin.h"
#include "plugins/system_volume_plugin.h"

namespace {
void print_help() {
    Logger::info("Commands:\n"
              "  help\n"
              "  list\n"
              "  pair <deviceId>\n"
              "  unpair <deviceId>\n"
              "  accept <deviceId>\n"
              "  reject <deviceId>\n"
              "  ping <deviceId> [message]\n"
              "  find <deviceId>\n"
              "  mpris <deviceId> list\n"
              "  mpris <deviceId> status [player]\n"
              "  mpris <deviceId> play|pause|playpause|stop|next|prev [player]\n"
              "  mpris <deviceId> volume <0-100> [player]\n"
              "  sinks <deviceId>\n"
              "  quit");
}
} // namespace

void prompt_device(
    const KdeConnectClient& client,
    std::istringstream& iss,
    const std::function<void(std::shared_ptr<KdeConnectClient::DeviceSession>)> &action) {

    std::string id;
    iss >> id;
    auto device = client.device(id);
    if (!device) {
        Logger::error("Device not found: " + id);
    }
    else {
        action(device);
    }
}

int main() {
    Storage storage;
    KdeConnectClient client(storage);
    if (!client.start()) {
        Logger::error("Failed to start client.");
        return 1;
    }

    Logger::info("MiniKDEConnect prototype running. Device ID: " + client.local_device().id);
    print_help();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "help") {
            print_help();
        } else if (cmd == "list") {
            auto sessions = client.devices();
            if (sessions.empty()) {
                Logger::info("No active devices.");
                continue;
            }
            for (const auto& [id, session] : sessions) {
                std::string state = session->paired ? "paired" : "unpaired";
                if (session->disconnected.load()) {
                    state = "disconnected";
                }
                Logger::info(session->info.name + " (" + id + ") - " + state);
            }
        } else if (cmd == "pair") {
            std::string id;
            iss >> id;
            client.request_pair(id);
        } else if (cmd == "accept") {
            std::string id;
            iss >> id;
            client.accept_pair(id);
        } else if (cmd == "reject") {
            std::string id;
            iss >> id;
            client.reject_pair(id);
        } else if (cmd == "unpair") {
            std::string id;
            iss >> id;
            client.unpair(id);
        } else if (cmd == "ping") {
            prompt_device(client, iss, [&iss](const std::shared_ptr<KdeConnectClient::DeviceSession> &device) {
                std::string message;
                std::getline(iss, message);
                if (!message.empty() && message.front() == ' ') message.erase(0, 1);
                device->plugin<PingPlugin>()->ping(message);
            });
        } else if (cmd == "find") {
            prompt_device(client, iss, [](const std::shared_ptr<KdeConnectClient::DeviceSession> &device) {
                device->plugin<FindMyPhonePlugin>()->find();
            });
        } else if (cmd == "mpris") {
            prompt_device(client, iss, [&iss](const std::shared_ptr<KdeConnectClient::DeviceSession> &device) {
                auto* mpris = device->plugin<MprisPlugin>();
                if (!mpris) return;

                std::string sub;
                iss >> sub;

                if (sub == "list") {
                    mpris->request_player_list();
                } else if (sub == "status") {
                    std::string player;
                    iss >> player;
                    if (player.empty()) player = mpris->current_player();
                    if (player.empty()) { Logger::warn("No active player."); return; }
                    mpris->request_status(player);
                } else if (sub == "volume") {
                    int vol = 50;
                    iss >> vol;
                    std::string player;
                    iss >> player;
                    if (player.empty()) player = mpris->current_player();
                    if (player.empty()) { Logger::warn("No active player."); return; }
                    mpris->set_volume(player, vol);
                } else {
                    // play / pause / playpause / stop / next / prev
                    static const std::unordered_map<std::string, std::string> action_map = {
                        {"play", "Play"}, {"pause", "Pause"}, {"playpause", "PlayPause"},
                        {"stop", "Stop"}, {"next", "Next"}, {"prev", "Previous"}
                    };
                    auto it = action_map.find(sub);
                    if (it == action_map.end()) { Logger::warn("Unknown mpris subcommand: " + sub); return; }

                    std::string player;
                    iss >> player;
                    if (player.empty()) player = mpris->current_player();
                    if (player.empty()) { Logger::warn("No active player."); return; }
                    mpris->send_action(player, it->second);
                }
            });
        } else if (cmd == "sinks") {
            prompt_device(client, iss, [](const std::shared_ptr<KdeConnectClient::DeviceSession> &device) {
                device->plugin<SystemVolumePlugin>()->send_sink_list();
            });
        } else if (cmd == "quit") {
            break;
        } else {
            Logger::warn("Unknown command. Type 'help'.");
        }
    }

    client.stop();
    Logger::shutdown();
    return 0;
}
