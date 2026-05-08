#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "kdeconnect_types.h"
#include "storage.h"
#include "tls.h"

class KdeConnectClient {
public:
  explicit KdeConnectClient(Storage storage);
  ~KdeConnectClient();

  bool start();
  void stop();

  void list_devices() const;
  void request_pair(const std::string &device_id);
  void accept_pair(const std::string &device_id);
  void reject_pair(const std::string &device_id);
  void unpair(const std::string &device_id);
  void send_ping(const std::string &device_id, const std::string &message);

  const DeviceInfo &local_device() const { return local_device_; }
  int tcp_port() const { return tcp_port_; }

private:
  struct DeviceSession;

  void tcp_accept_loop();
  void udp_listen_loop();
  void udp_broadcast_loop();
  void handle_discovered_peer(const DeviceInfo &identity,
                              const std::string &host, int port);
  void handle_new_connection(const DeviceInfo &identity, int fd,
                             bool tcp_server_side);
  void read_loop(const std::shared_ptr<DeviceSession> &session);

  void handle_packet(const std::shared_ptr<DeviceSession> &session,
                     const std::string &line);
  void handle_pair_packet(const std::shared_ptr<DeviceSession> &session,
                          const nlohmann::json &body);
  void handle_ping_packet(const std::shared_ptr<DeviceSession> &session,
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

  std::thread tcp_thread_;
  std::thread udp_thread_;
  std::thread broadcast_thread_;

  mutable std::mutex session_mutex_;
  std::unordered_map<std::string, std::shared_ptr<DeviceSession>> sessions_;
};