#pragma once

#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "../net/kdeconnect_types.h"
#include "../net/network_packet.h"
#include "utils/logger.h"

class Plugin;
struct TlsSession;

// Interface for plugins to interact with the device session
class DeviceProvider {
public:
    struct DeviceSession;
    virtual ~DeviceProvider() = default;
    
    // Send a packet to this specific device
    virtual bool send_packet(const std::string& device_id, const NetworkPacket& pkt) = 0;
    virtual std::shared_ptr<DeviceSession> device(const std::string &device_id) const = 0;
    virtual std::unordered_map<std::string, std::shared_ptr<DeviceSession>> devices() const = 0;

    struct DeviceSession {
        DeviceInfo info;
        PairState pair_state = PairState::NotPaired;
        long pairing_timestamp = 0;
        bool paired = false;
        std::string cert_pem;
        std::vector<unsigned char> peer_pubkey;
        std::string peer_host;

        std::unique_ptr<TlsSession> tls;
        int fd = -1;
        std::thread io_thread;
        std::mutex send_queue_mutex;
        std::queue<std::string> send_queue;
        std::atomic<bool> disconnected{false};

        std::vector<std::unique_ptr<Plugin>> plugins;

        template<typename T>
        T* plugin() {
            for (const auto& plugin : plugins) {
                if (auto typed_plugin = dynamic_cast<T*>(plugin.get())) {
                    return typed_plugin;
                }
            }
            Logger::error("Plugin of requested type not found for device " + info.name + " (" + info.id + ")");
            return nullptr;
        }
    };
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
    virtual void on_connected(bool paired) {}
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
