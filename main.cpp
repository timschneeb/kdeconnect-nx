#include <iostream>
#include <sstream>
#include <string>

#include "kdeconnect_client.h"
#include "src/utils/logger.h"
#include "plugins/ping_plugin.h"

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
                if (!message.empty() && message.front() == ' ') {
                    message.erase(0, 1);
                }

                device->plugin<PingPlugin>()->ping(message);
            });
        } else if (cmd == "quit") {
            break;
        } else {
            Logger::warn("Unknown command. Type 'help'.");
        }
    }

    client.stop();
    return 0;
}