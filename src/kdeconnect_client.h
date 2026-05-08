#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "kdeconnect_types.h"
#include "mdns/mdns_discovery.h"
#include "storage.h"
#include "tls.h"
#include "plugins/plugin.h"
#include "utils/logger.h"

class KdeConnectClient : public DeviceProvider {
public:
  struct DeviceSession;

  explicit KdeConnectClient(Storage storage);
  ~KdeConnectClient() override;

  bool start();
  void stop();

  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> devices() const;
  std::shared_ptr<DeviceSession> device(const std::string &device_id) const;

  void request_pair(const std::string &device_id);
  void accept_pair(const std::string &device_id);
  void reject_pair(const std::string &device_id);
  void unpair(const std::string &device_id);

  bool send_packet(const std::string &device_id, const NetworkPacket& pkt) override;

  const DeviceInfo &local_device() const { return local_device_; }
  int tcp_port() const { return tcp_port_; }

  struct DeviceSession {
      DeviceInfo info;
      PairState pair_state = PairState::NotPaired;
      long pairing_timestamp = 0;
      bool paired = false;
      std::string cert_pem;
      std::vector<unsigned char> peer_pubkey;

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

private:
  void tcp_accept_loop();
  void udp_listen_loop();
  void udp_broadcast_loop();
  void send_udp_identity_probe(const std::string& device_id, const std::string& host);
  void handle_discovered_peer(const DeviceInfo &identity,
                              const std::string &host, int port);
  void handle_new_connection(const DeviceInfo &identity, int fd,
                             bool tcp_server_side);
  void io_loop(const std::shared_ptr<DeviceSession> &session);

  void handle_packet(const std::shared_ptr<DeviceSession> &session,
                     const std::string &line);
  void handle_pair_packet(const std::shared_ptr<DeviceSession> &session,
                          const nlohmann::json &body);

  std::string verification_key(const std::shared_ptr<DeviceSession> &session,
                               long timestamp) const;

  Storage storage_;
  DeviceInfo local_device_;
  TlsContext tls_;

  std::atomic<bool> running_{false};

  int tcp_fd_ = -1;
  int udp_fd_ = -1;
  int tcp_port_ = 0;

  std::unique_ptr<MdnsDiscovery> mdns_discovery_;

  std::thread tcp_thread_;
  std::thread udp_thread_;
  std::thread broadcast_thread_;

  mutable std::mutex session_mutex_;
  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> sessions_;
};