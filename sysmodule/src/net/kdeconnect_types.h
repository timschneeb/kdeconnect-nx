#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr int kProtocolVersion = 8;

struct DeviceInfo {
    std::string id;
    std::string name;
    std::string type;
    int protocol_version = kProtocolVersion;
    std::vector<std::string> incoming_capabilities;
    std::vector<std::string> outgoing_capabilities;
};

enum class PairState {
    NotPaired,
    Requested,
    RequestedByPeer,
    Paired
};

