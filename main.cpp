#include <iostream>
#include <sstream>
#include <string>

#include "kdeconnect_client.h"

namespace {
void print_help() {
    std::cout << "Commands:\n"
              << "  help\n"
              << "  list\n"
              << "  pair <deviceId>\n"
              << "  unpair <deviceId>\n"
              << "  accept <deviceId>\n"
              << "  reject <deviceId>\n"
              << "  ping <deviceId> [message]\n"
              << "  quit\n";
}
} // namespace

int main() {
    Storage storage;
    KdeConnectClient client(storage);
    if (!client.start()) {
        std::cerr << "Failed to start client.\n";
        return 1;
    }

    std::cout << "MiniKDEConnect prototype running. Device ID: " << client.local_device().id << "\n";
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
            client.list_devices();
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
            std::string id;
            iss >> id;
            std::string message;
            std::getline(iss, message);
            if (!message.empty() && message.front() == ' ') {
                message.erase(0, 1);
            }
            client.send_ping(id, message);
        } else if (cmd == "quit") {
            break;
        } else {
            std::cout << "Unknown command. Type 'help'.\n";
        }
    }

    client.stop();
    return 0;
}