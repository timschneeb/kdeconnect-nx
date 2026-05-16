#pragma once
#include <functional>
#include <memory>
#include <string>
#include "../net/kdeconnect_types.h"
class MdnsDiscovery {
public:
    // device_id may be empty if not resolved; host is IPv4 string
    using PeerFoundCallback = std::function<void(const std::string& device_id, const std::string& host)>;
    MdnsDiscovery(const DeviceInfo& local_device, int tcp_port, PeerFoundCallback on_peer_found);
    ~MdnsDiscovery();
    bool start() const;
    void stop() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
