#pragma once

#include <string>
#include <vector>
#include "../network_packet.h"

// Interface for plugins to interact with the device session
class DeviceProvider {
public:
    virtual ~DeviceProvider() = default;
    
    // Send a packet to this specific device
    virtual bool send_packet(const std::string& device_id, const NetworkPacket& pkt) = 0;
};

// Base class for all MiniKDEConnect plugins
class Plugin {
public:
    virtual ~Plugin() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;

    // Returns a list of packet types this plugin handles
    virtual std::vector<std::string> supported_packet_types() const = 0;

    // Returns a list of packet types this plugin can send out
    virtual std::vector<std::string> outgoing_packet_types() const = 0;

    // Initialize the plugin with its device context
    virtual void init(DeviceProvider* provider, const std::string& device_id) {
        provider_ = provider;
        device_id_ = device_id;
        on_create();
    }

    // Called when a packet is received. Returns true if handled.
    virtual bool on_packet_received(const NetworkPacket& np) {
        return false;
    }

    virtual void on_create() {}
    virtual void on_destroy() {}

protected:
    void send_packet(const NetworkPacket& np) const {
        if (provider_) {
            provider_->send_packet(device_id_, np);
        }
    }

    DeviceProvider* provider_ = nullptr;
    std::string device_id_;
};
