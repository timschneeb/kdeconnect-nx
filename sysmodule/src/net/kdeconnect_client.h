#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "kdeconnect_types.h"
#include "tls.h"

#include "../mdns/mdns_discovery.h"
#include "../plugins/plugin.h"
#include "../utils/scoped_fd.h"
#include "../utils/stack_thread.h"
#include "../utils/storage.h"

class KdeConnectClient : public DeviceProvider {
public:
  explicit KdeConnectClient(Storage storage);
  ~KdeConnectClient() override;

  bool start(bool enable_mdns);
  void stop();
  bool needs_restart() const { return needs_restart_.load(); }

  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> devices() const override;
  std::shared_ptr<DeviceSession> device(const std::string &device_id) const override;
  std::vector<PairedDeviceInfo> offline_paired_devices() const;

  void send_broadcast();
  void request_pair(const std::string &device_id);
  void accept_pair(const std::string &device_id);
  void reject_pair(const std::string &device_id);
  void unpair(const std::string &device_id);

  bool send_packet(const std::string &device_id, const NetworkPacket& pkt) override;
  bool send_payload(const std::string &device_id, NetworkPacket pkt) override;
  bool download_payload(const std::shared_ptr<DeviceSession> &session,
                        NetworkPacket &packet,
                        const std::string& file_path) override;
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
                     const std::string &line) const;
  void handle_pair_packet(const std::shared_ptr<DeviceSession> &session,
                          const JsonBody &body) const;

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

  StackThread network_thread_;
  StackThread broadcast_thread_;

  mutable std::mutex session_mutex_;
  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> sessions_;

  struct PendingTask {
    StackThread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };

  void reap_pending_threads_locked(); // call with pending_mutex_ held

  std::mutex pending_mutex_;
  std::vector<PendingTask> pending_threads_;

  // Throttle mDNS-triggered probes to once per second per device.
  static constexpr auto kMdnsProbeCooldown = std::chrono::seconds(1);
  mutable std::mutex mdns_probe_cooldown_mutex_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> mdns_probe_cooldown_;
};