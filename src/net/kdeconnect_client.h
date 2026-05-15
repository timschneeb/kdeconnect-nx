#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../kdeconnect_types.h"
#include "../mdns/mdns_discovery.h"
#include "../storage.h"
#include "tls.h"
#include "../plugins/plugin.h"
#include "../utils/logger.h"
#include "../utils/scoped_fd.h"

class KdeConnectClient : public DeviceProvider {
public:
  explicit KdeConnectClient(Storage storage);
  ~KdeConnectClient() override;

  bool start();
  void stop();
  bool needs_restart() const { return needs_restart_.load(); }

  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> devices() const override;
  std::shared_ptr<DeviceSession> device(const std::string &device_id) const override;

  void request_pair(const std::string &device_id);
  void accept_pair(const std::string &device_id);
  void reject_pair(const std::string &device_id);
  void unpair(const std::string &device_id);

  bool send_packet(const std::string &device_id, const NetworkPacket& pkt) override;

  const DeviceInfo &local_device() const { return local_device_; }
  int tcp_port() const { return tcp_port_; }

private:
  void network_loop();
  void udp_broadcast_loop();
  void send_udp_identity_probe(const std::string& device_id, const std::string& host);
  void handle_discovered_peer(const DeviceInfo &identity,
                              const std::string &host, int port);
  void handle_new_connection(const DeviceInfo &identity, ScopedFd fd,
                             bool tcp_server_side);
  void io_loop(const std::shared_ptr<DeviceSession> &session);

  void handle_packet(const std::shared_ptr<DeviceSession> &session,
                     const std::string &line);
  void handle_pair_packet(const std::shared_ptr<DeviceSession> &session,
                          const nlohmann::json &body);
  void download_payload(const std::shared_ptr<DeviceSession> &session,
                        NetworkPacket &packet);

  std::string verification_key(const std::shared_ptr<DeviceSession> &session,
                               long timestamp) const;

  Storage storage_;
  DeviceInfo local_device_;
  TlsContext tls_;

  std::atomic<bool> running_{false};
  std::atomic<bool> needs_restart_{false};

  int tcp_fd_ = -1;
  int udp_fd_ = -1;
  int tcp_port_ = 0;

  std::unique_ptr<MdnsDiscovery> mdns_discovery_;

  std::thread network_thread_;
  std::thread broadcast_thread_;

  mutable std::mutex session_mutex_;
  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> sessions_;
};