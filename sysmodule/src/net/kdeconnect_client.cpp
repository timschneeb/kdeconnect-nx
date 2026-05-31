#include "kdeconnect_client.h"

#include "network_packet.h"

#include <mbedtls/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <new>
#include <optional>
#include <sys/select.h>
#include <thread>
#if defined(__SWITCH__)
#include <switch.h>
#endif
#include "../plugins/plugin_registry.h"
#include "../plugins/notification_plugin.h"
#include "../utils/settings_store.h"

#include "utils/logger.h"
#include "../utils/network_util.h"

namespace {
constexpr int kUdpPort = 1716;
constexpr int kMinTcpPort = 1716;
constexpr int kMaxTcpPort = 1764;
constexpr size_t kMaxPacketSize = 512 * 1024;
constexpr size_t kMaxUdpPacketSize = 8 * 1024;
constexpr int kPairingWindowSeconds = 1800;
constexpr int kKeepaliveIdleSeconds = 8;
constexpr int kKeepaliveIntervalSeconds = 3;
constexpr int kKeepaliveProbeCount = 3;
constexpr int kConnectTimeoutMs = 3000;

void configure_tcp_keepalive(int fd) {
    int keepalive = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &kKeepaliveIdleSeconds, sizeof(kKeepaliveIdleSeconds));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &kKeepaliveIntervalSeconds, sizeof(kKeepaliveIntervalSeconds));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &kKeepaliveProbeCount, sizeof(kKeepaliveProbeCount));
}

bool connect_with_timeout(int fd, const sockaddr_in& addr, int timeout_ms) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return false;
    }

    int result = connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (result == 0) {
        fcntl(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS) {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    result = select(fd + 1, nullptr, &wfds, nullptr, &tv);
    if (result <= 0) {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    fcntl(fd, F_SETFL, flags);
    return true;
}

} // namespace

DeviceProvider::DeviceSession::~DeviceSession() {
    // Safety net: if this destructor is reached with io_thread still joinable,
    // the thread is the sole owner of 'this'. Detach
    // rather than join to avoid std::terminate. io_loop has already returned at
    // this point so the thread exits on its own.
    if (io_thread.joinable()) io_thread.detach();
}

KdeConnectClient::KdeConnectClient(Storage storage) : storage_(std::move(storage)) {
    local_device_ = storage_.load_or_create_local_device(this);
}

KdeConnectClient::~KdeConnectClient() {
    stop();
}

bool KdeConnectClient::start(bool enable_mdns) {
    needs_restart_.store(false);

    if (running_.load()) {
        return true;
    }

    if (!tls_.load_or_create(storage_.cert_path(), storage_.key_path())) {
        Logger::error("Failed to load or create TLS identity.");
        needs_restart_.store(true);
        return false;
    }
    local_device_.id = tls_.device_id();

    tcp_fd_ = NetworkUtil::create_tcp_server_socket(kMinTcpPort, kMaxTcpPort, tcp_port_);
    if (tcp_fd_ < 0) {
        Logger::error("Failed to bind TCP socket on ports 1716-1764.");
        needs_restart_.store(true);
        return false;
    }

    udp_fd_ = NetworkUtil::create_udp_broadcast_socket(kUdpPort);
    if (udp_fd_ < 0) {
        Logger::warn("Unable to bind UDP socket for listening; discovery receive disabled.");
    }

    if (enable_mdns) {
        mdns_discovery_ = std::make_unique<MdnsDiscovery>(local_device_, tcp_port_, [this](const std::string& device_id, const std::string& host) {
            {
                std::lock_guard lock(session_mutex_);
                if (sessions_.contains(device_id) && !sessions_[device_id]->disconnected.load()) {
                    // already connected
                    return;
                }
            }

            // mDNS fires continuously; throttle to one probe per device per second.
            {
                std::lock_guard lock(mdns_probe_cooldown_mutex_);
                auto now = std::chrono::steady_clock::now();
                auto& last = mdns_probe_cooldown_[device_id];
                if ((now - last) < kMdnsProbeCooldown) {
                    Logger::info("mDNS: Skipped probe to %s due to cooldown", device_id.c_str());
                    return;
                }
                last = now;
                if (mdns_probe_cooldown_.size() > 32) {
                    auto oldest = mdns_probe_cooldown_.begin();
                    for (auto it = std::next(oldest); it != mdns_probe_cooldown_.end(); ++it)
                        if (it->second < oldest->second) oldest = it;
                    mdns_probe_cooldown_.erase(oldest);
                }
            }

            Logger::info("mDNS: Sending probe to %s at %s", device_id.c_str(), host.c_str());
            send_udp_identity_probe(device_id, host);
        });
    }

    needs_restart_.store(false);
    running_.store(true);
    network_thread_ = StackThread(24 * 1024, "kc-network", &KdeConnectClient::network_loop, this);
    broadcast_thread_ = StackThread(4 * 1024, "kc-broadcast", &KdeConnectClient::udp_broadcast_loop, this);

    Logger::info("Listening on TCP port %d.", tcp_port_);

    if (!mdns_discovery_) {
        Logger::warn("mDNS discovery is disabled");
    }
    else if (!mdns_discovery_->start()) {
        Logger::warn("mDNS discovery could not be started; continuing with UDP broadcast discovery only.");
        mdns_discovery_.reset();
    }
    return true;
}

void KdeConnectClient::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (mdns_discovery_) {
        mdns_discovery_->stop();
        mdns_discovery_.reset();
    }

    if (network_thread_.joinable()) {
        network_thread_.join();
    }
    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();
    }

    if (tcp_fd_ >= 0) {
        close(tcp_fd_);
        tcp_fd_ = -1;
    }
    if (udp_fd_ >= 0) {
        close(udp_fd_);
        udp_fd_ = -1;
    }

    // Join all pending handshake threads (network_loop has exited, so no new ones are added).
    // SO_RCVTIMEO on their sockets bounds the wait to ~5 s in the worst case.
    {
        std::lock_guard lock(pending_mutex_);
        for (auto& task : pending_threads_) {
            if (task.thread.joinable()) task.thread.join();
        }
        pending_threads_.clear();
    }

    std::vector<std::shared_ptr<DeviceSession>> to_stop;
    {
        std::lock_guard lock(session_mutex_);
        for (auto &[_, session]: sessions_) {
            to_stop.push_back(session);
        }
        sessions_.clear();
    }
    for (auto& session : to_stop) {
        session->disconnected.store(true);
        if (session->fd >= 0) {
            shutdown(session->fd, SHUT_RDWR);
            close(session->fd);
            session->fd = -1;
        }
        if (session->io_thread.joinable()) session->io_thread.join();
        if (session->tls) {
            mbedtls_ssl_close_notify(&session->tls->ssl);
            session->tls.reset();
        }
    }
}

void KdeConnectClient::send_udp_identity_probe(const std::string& device_id, const std::string& host) {
    if (udp_fd_ < 0 || host.empty()) {
        return;
    }

    NetworkPacket identity = NetworkUtil::make_identity_packet(local_device_, device_id.empty() ? std::nullopt : std::optional<std::string>(device_id), std::nullopt, tcp_port_);
    std::string payload = identity.serialize();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kUdpPort);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        Logger::warn("Ignoring mDNS peer with invalid IPv4 address: %s", host.c_str());
        return;
    }

    sendto(udp_fd_, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

void KdeConnectClient::reap_pending_threads_locked() {
    std::erase_if(pending_threads_,
                  [](PendingTask& task) {
                      if (task.done->load()) {
                          if (task.thread.joinable()) task.thread.join();
                          return true;
                      }
                      return false;
                  });
}

void KdeConnectClient::network_loop() {
    while (running_.load()) {
        {
            std::lock_guard lock(pending_mutex_);
            reap_pending_threads_locked();
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        int nfds = 0;
        if (tcp_fd_ >= 0) { FD_SET(tcp_fd_, &rfds); nfds = std::max(nfds, tcp_fd_ + 1); }
        if (udp_fd_ >= 0) { FD_SET(udp_fd_, &rfds); nfds = std::max(nfds, udp_fd_ + 1); }
        if (nfds == 0) break;

        timeval tv{0, 50'000}; // 50 ms: exit quickly when running_ goes false
        if (select(nfds, &rfds, nullptr, nullptr, &tv) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (tcp_fd_ >= 0 && FD_ISSET(tcp_fd_, &rfds)) {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            ScopedFd fd{accept(tcp_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len)};
            if (!fd) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                Logger::warn("TCP accept failed (%s)", strerror(errno));
                needs_restart_.store(true);
                break;
            } else {
                configure_tcp_keepalive(fd.raw);
                // Bound blocking time: if peer stalls during identity read or TLS handshake.
                timeval recv_timeout{5, 0};
                setsockopt(fd.raw, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
                // Offload to a thread so network_loop is never blocked by a slow peer.
                auto done = std::make_shared<std::atomic<bool>>(false);

                const int raw_fd = fd.release();
                auto t = StackThread(24 * 1024, "kc-accept", [this, raw_fd, done]() mutable {
                    ScopedFd fd{raw_fd};
                    try {
                        [&]() {
                            auto line = NetworkUtil::read_line_fd(fd.raw, kMaxPacketSize);
                            auto packet = line ? NetworkPacket::parse(*line) : std::optional<NetworkPacket>{};
                            bool valid = packet && packet->type == PacketTypes::Identity;
                            if (valid && packet->body.has("targetDeviceId")) {
                                const auto tid = packet->body.value("targetDeviceId", "");
                                if (!tid.empty() && tid != local_device_.id) valid = false;
                            }
                            if (valid && packet->body.has("targetProtocolVersion")) {
                                if (packet->body.value("targetProtocolVersion", kProtocolVersion) != kProtocolVersion)
                                    valid = false;
                            }
                            if (valid)
                                handle_new_connection(NetworkUtil::info_from_identity(*packet), std::move(fd), true);
                        }();
                    } catch (const std::bad_alloc&) {
                        Logger::error("Out of memory in incoming connection handler");
                    }
                    done->store(true);
                });
                std::lock_guard lock(pending_mutex_);
                pending_threads_.push_back({std::move(t), std::move(done)});
            }
        }

        if (udp_fd_ >= 0 && FD_ISSET(udp_fd_, &rfds)) {
            std::array<char, kMaxUdpPacketSize> buffer{};
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            ssize_t len = recvfrom(udp_fd_, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<sockaddr*>(&from), &from_len);
            if (len > 0) {
                auto packet = NetworkPacket::parse(std::string(buffer.data(), static_cast<size_t>(len)));
                if (packet && packet->type == PacketTypes::Identity) {
                    DeviceInfo identity = NetworkUtil::info_from_identity(*packet);
                    if (!identity.id.empty() && identity.id != local_device_.id) {
                        char addr_str[INET_ADDRSTRLEN] = {};
                        inet_ntop(AF_INET, &from.sin_addr, addr_str, sizeof(addr_str));
                        identity.protocol_version = packet->body.value("protocolVersion", kProtocolVersion);
                        handle_discovered_peer(identity, addr_str, packet->body.value("tcpPort", kMinTcpPort));
                    }
                }
            }
        }
    }
}

void KdeConnectClient::send_broadcast() {
    if (udp_fd_ < 0) return;
    Logger::info("Sending single UDP broadcast...");
    NetworkPacket identity = NetworkUtil::make_identity_packet(local_device_, std::nullopt, std::nullopt, tcp_port_);
    std::string payload = identity.serialize();
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kUdpPort);
    addr.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(udp_fd_, payload.data(), payload.size(), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

void KdeConnectClient::udp_broadcast_loop() {
    int broadcast_count = 0;
    while (running_.load() && broadcast_count < 5) {
        NetworkPacket identity = NetworkUtil::make_identity_packet(local_device_, std::nullopt, std::nullopt, tcp_port_);
        std::string payload = identity.serialize();

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kUdpPort);
        addr.sin_addr.s_addr = INADDR_BROADCAST;

        if (udp_fd_ >= 0) {
            sendto(udp_fd_, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        }
        for (int i = 0; i < 20 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        broadcast_count++;
    }
}

void KdeConnectClient::handle_discovered_peer(const DeviceInfo& identity, const std::string& host, int port) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto t = StackThread(24 * 1024, "kc-connect", [this, identity, host, port, done]() {
        try {
            [&]() {
                ScopedFd fd{socket(AF_INET, SOCK_STREAM, 0)};
                if (!fd) return;
                configure_tcp_keepalive(fd.raw);
                timeval recv_timeout{5, 0};
                setsockopt(fd.raw, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(static_cast<uint16_t>(port));
                if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) return;
                if (!connect_with_timeout(fd.raw, addr, kConnectTimeoutMs)) return;

                NetworkPacket my_identity = NetworkUtil::make_identity_packet(local_device_, identity.id, identity.protocol_version, std::nullopt);
                std::string payload = my_identity.serialize();
                send(fd.raw, payload.data(), payload.size(), 0);

                handle_new_connection(identity, std::move(fd), false);
            }();
        } catch (const std::bad_alloc&) {
            Logger::error("Out of memory connecting to %s", identity.name.c_str());
        }
        done->store(true);
    });
    std::lock_guard lock(pending_mutex_);
    pending_threads_.push_back({std::move(t), std::move(done)});
}

void KdeConnectClient::handle_new_connection(const DeviceInfo& identity, ScopedFd fd, bool tcp_server_side) {
    if (identity.id.empty() || identity.id == local_device_.id) return;

    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(identity.id);
        if (it != sessions_.end() && !it->second->disconnected.load()) {
            // TODO: allow old but alive connections to be replaced?
            Logger::warn("Already connected to %s (%s) but a new connection was received.",
                     identity.name.c_str(), identity.id.c_str());
            return;
        }
    }

    auto stored = storage_.load_paired_device(identity.id);
    bool paired = stored.has_value();
    std::string stored_pem = stored ? stored->certificate_pem : std::string();

    auto tls_session = tls_.create_session(fd.raw, tcp_server_side);
    if (!tls_session) {
        Logger::error("Failed to create TLS session for device %s (%s)", identity.id.c_str(),
                  tcp_server_side ? "server-side" : "client-side");
        return;
    }

    if (!NetworkUtil::perform_tls_handshake(*tls_session)) {
        Logger::error("TLS handshake failed for device %s (%s)", identity.id.c_str(),
                  tcp_server_side ? "server-side" : "client-side");
        return;
    }

    std::string peer_pem = TlsContext::peer_cert_pem(*tls_session);
    if (peer_pem.empty() && tcp_server_side) {
        Logger::warn("Peer did not present a TLS certificate for device %s (server-side)", identity.id.c_str());
        return;
    }

    if (paired && !stored_pem.empty() && peer_pem != stored_pem && tcp_server_side) {
        Logger::warn("Certificate mismatch for paired device %s, aborting connection", identity.id.c_str());
        return;
    }

    NetworkPacket secure_identity = NetworkUtil::make_identity_packet(local_device_, std::nullopt, std::nullopt, std::nullopt);
    if (!NetworkUtil::send_all_tls(*tls_session, secure_identity.serialize())) {
        Logger::error("Failed to send identity packet to %s (%s) after TLS handshake.",
                  identity.name.c_str(), identity.id.c_str());
        return;
    }

    auto line = NetworkUtil::read_line_tls(*tls_session, kMaxPacketSize);
    if (!line) {
        Logger::error("Failed to read data from %s (%s) after TLS handshake.",
                  identity.name.c_str(), identity.id.c_str());
        return;
    }
    auto packet = NetworkPacket::parse(*line);
    if (!packet || packet->type != PacketTypes::Identity) {
        Logger::error("Failed to parse packet from %s (%s) after TLS handshake.",
                  identity.name.c_str(), identity.id.c_str());
        return;
    }

    DeviceInfo secure_info = NetworkUtil::info_from_identity(*packet);
    if (secure_info.id != identity.id || secure_info.protocol_version != identity.protocol_version) {
        Logger::error("Identity mismatch for %s (%s) after TLS handshake.",
                  identity.name.c_str(), identity.id.c_str());
        return;
    }

    auto session = std::make_shared<DeviceSession>();
    session->info = secure_info;
    session->paired = paired;
    session->pair_state = paired ? PairState::Paired : PairState::NotPaired;
    session->tls = std::move(tls_session);
    session->fd = fd.release();
    session->cert_pem = peer_pem;
    session->peer_pubkey = TlsContext::peer_pubkey_bytes(*session->tls);

    {
        sockaddr_in peer_addr{};
        socklen_t peer_addr_len = sizeof(peer_addr);
        if (getpeername(session->fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len) == 0) {
            char addr_str[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &peer_addr.sin_addr, addr_str, sizeof(addr_str)))
                session->peer_host = addr_str;
        }
    }

    PluginRegistry::instantiate_plugins(this, session->info.id, session->plugins);

    session->io_thread = StackThread(48 * 1024, "kc-io", &KdeConnectClient::io_loop, this, session);

    std::shared_ptr<DeviceSession> old_session;
    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(session->info.id);
        if (it != sessions_.end()) {
            old_session = it->second;
        }
        sessions_[session->info.id] = session;
    }

    // Clean up the old session outside the lock. Joining under the lock would
    // deadlock: the old read_loop calls send_packet → device() → session_mutex_.
    if (old_session) {
        old_session->disconnected.store(true);
        if (old_session->fd >= 0) {
            shutdown(old_session->fd, SHUT_RDWR);
            close(old_session->fd);
            old_session->fd = -1;
        }
        if (old_session->io_thread.joinable()) old_session->io_thread.join();
        if (old_session->tls) {
            mbedtls_ssl_close_notify(&old_session->tls->ssl);
            old_session->tls.reset();
        }
        old_session.reset();
    }
    malloc_trim(0);

    Logger::info("Connected to %s (%s, %s, %s).",
             session->info.name.c_str(), session->info.id.c_str(),
             tcp_server_side ? "server-side" : "client-side",
             session->paired ? "paired" : "unpaired");

    if (session->paired && SettingsStore::get(KdecBoolSettingKey::NotificationShowOnConnect)) {
        NotificationPlugin::post_notification(
            "kdeconnect", session->info.name, "Connected",
            "connect_" + session->info.id);
    }

    if (!session->paired) {
        // Send an unpair packet in case the remote still thinks we are connected after doing an offline unpair
        NetworkPacket pkt;
        pkt.type = PacketTypes::Pair;
        pkt.body.set("pair", false);
        send_packet(identity.id, pkt);
    }

    // Quirk: the desktop client will not display us as connected, until we send another packet, so just resend the identity packet.
    send_packet(identity.id, NetworkUtil::make_identity_packet(local_device_, identity.id, identity.protocol_version, tcp_port_));

    for (auto& plugin : session->plugins) {
        plugin->on_connected(session->paired);
    }
}


void KdeConnectClient::handle_packet(const std::shared_ptr<DeviceSession>& session, const std::string& line) {
    auto packet = NetworkPacket::parse(line);
    if (!packet) return;

    if (packet->type == PacketTypes::Pair) {
        handle_pair_packet(session, packet->body);
        return;
    }

    if (!session->paired) return;

    for (const auto& plugin : session->plugins) {
        for (const auto& supported_type : plugin->supported_packet_types()) {
            if (supported_type == packet->type) {
                if (plugin->on_packet_received(*packet)) {
                    return;
                }
            }
        }
    }
}

bool KdeConnectClient::download_payload(const std::shared_ptr<DeviceSession>& session,
                                        NetworkPacket& packet,
                                        const std::string& file_path) {
    if (!session) return false;
    ScopedFd fd{socket(AF_INET, SOCK_STREAM, 0)};
    if (!fd) return false;

    // Allow aborting mid-transfer if session is torn down
    timeval payload_timeout{0, 500'000};
    setsockopt(fd.raw, SOL_SOCKET, SO_RCVTIMEO, &payload_timeout, sizeof(payload_timeout));
    // Maximise TCP receive window to keep the phone's send pipeline full
    const int rcvbuf = 8 * 1024;
    setsockopt(fd.raw, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(packet.payload_port));
    if (inet_pton(AF_INET, session->peer_host.c_str(), &addr.sin_addr) != 1) return false;
    if (connect(fd.raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        Logger::error("Failed to connect to peer for payload download (port %d): %s", packet.payload_port, strerror(errno));
        return false;
    }

    auto tls_session = tls_.create_session(fd.raw, true);
    if (!tls_session || !NetworkUtil::perform_tls_handshake(*tls_session)) {
        Logger::error("download_payload: TLS handshake failed");
        return false;
    }

    if (!file_path.empty()) {
        // Stream directly to file
#if defined(__SWITCH__)
        constexpr size_t kChunkSize = 16384 * 4;
        auto chunk = std::make_unique<uint8_t[]>(kChunkSize);
        int64_t remaining = packet.payload_size;

        FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
        if (!fs) {
            Logger::error("downloadPayload: fsdevGetDeviceFileSystem failed");
            mbedtls_ssl_close_notify(&tls_session->ssl);
            return false;
        }

        char path_buf[FS_MAX_PATH];
        snprintf(path_buf, sizeof(path_buf), "%s", file_path.c_str());

        {
            const auto slash = file_path.rfind('/');
            if (slash != std::string::npos)
                Storage::make_directories(file_path.substr(0, slash));
        }

        fsFsDeleteFile(fs, path_buf);

        Result rc;
        if (rc = fsFsCreateFile(fs, path_buf, static_cast<s64>(packet.payload_size), 0); R_FAILED(rc)) {
            Logger::error("Failed to create %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
            mbedtls_ssl_close_notify(&tls_session->ssl);
            return false;
        }

        FsFile file;

        if (rc = fsFsOpenFile(fs, path_buf, FsOpenMode_Write, &file); R_FAILED(rc)) {
            Logger::error("Failed to open %s for writing: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
            mbedtls_ssl_close_notify(&tls_session->ssl);
            return false;
        }

        u64 offset = 0;
        size_t chunk_fill = 0;
        bool write_error = false;

        auto flush_chunk = [&]() -> bool {
            if (chunk_fill == 0) return true;
            rc = fsFileWrite(&file, offset, chunk.get(), chunk_fill, FsWriteOption_None);
            if (R_FAILED(rc)) {
                Logger::error("Failed to write to %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
                return false;
            }
            offset += chunk_fill;
            chunk_fill = 0;
            return true;
        };

        while (remaining > 0 && !session->disconnected.load()) {
            const size_t to_read = std::min(static_cast<size_t>(remaining), kChunkSize - chunk_fill);
            const int ret = mbedtls_ssl_read(&tls_session->ssl, chunk.get() + chunk_fill, to_read);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ) continue;
            if (ret <= 0) break;
            chunk_fill += static_cast<size_t>(ret);
            remaining -= ret;
            if (chunk_fill == kChunkSize || remaining == 0) {
                if (!flush_chunk()) { write_error = true; break; }
            }
        }
        if (!write_error && !flush_chunk()) write_error = true;
        fsFileFlush(&file);
        fsFileClose(&file);

        mbedtls_ssl_close_notify(&tls_session->ssl);
        tls_session.reset();
        chunk.reset();
        malloc_trim(0);

        const bool aborted = session->disconnected.load() || remaining > 0 || write_error;
        if (aborted) {
            fsFsDeleteFile(fs, path_buf);
            Logger::info("Download aborted. Remaining bytes: %ld", remaining);
            return false;
        }
        Logger::info("Streamed payload to %s", file_path.c_str());
        return true;
#else
        return false; // file streaming not supported on non-Switch
#endif
    } else {
        // TODO: deprecate memory buffer mode
        //       migrate from stbi to libpng so we can stream directly from downloaded png file to rgba8 file.

        // Buffer to memory with a 64KB cap (used for small payloads like app icons).
        if (packet.payload_size > 65536) {
            Logger::warn("Payload size %ld exceeds in-memory cap, truncating.", packet.payload_size);
        }

        const int64_t cap = std::min(packet.payload_size, static_cast<int64_t>(65536));
        packet.payload.resize(static_cast<size_t>(cap));

        size_t received = 0;
        while (received < static_cast<size_t>(cap) && !session->disconnected.load()) {
            int ret = mbedtls_ssl_read(&tls_session->ssl,
                                       packet.payload.data() + received,
                                       static_cast<size_t>(cap) - received);
            if (ret <= 0) break;
            received += static_cast<size_t>(ret);
        }

        mbedtls_ssl_close_notify(&tls_session->ssl);
        tls_session.reset();
        malloc_trim(0);
        packet.payload.resize(received);
        Logger::info("Downloaded payload: %zu bytes for %s", received, packet.type.c_str());
        return true;
    }
}

void KdeConnectClient::handle_pair_packet(const std::shared_ptr<DeviceSession>& session, const JsonBody& body) {
    bool wants_pair = body.value("pair", false);
    Logger::info("Got pair packet from %s (wants_pair=%d) %s", session->info.name.c_str(), wants_pair, body.dump().c_str());

    if (wants_pair) {
        int64_t timestamp = body.value("timestamp", (int64_t)0);
        long now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        if (timestamp != 0 && std::llabs(now - timestamp) > kPairingWindowSeconds) {
            Logger::warn("Pair request rejected due to clock skew.");
            NotificationPlugin::post_notification("kdeconnect", session->info.name,
                "Pair request rejected due to clock skew. Check the system time.", session->info.id + "_error");
            return;
        }

        if (session->pair_state == PairState::Requested) {
            if (session->cert_pem.empty()) {
                Logger::warn("Pairing failed for %s: missing peer certificate", session->info.name.c_str());
                return;
            }
            session->pair_state = PairState::Paired;
            session->paired = true;
            storage_.save_paired_device(session->info, session->cert_pem);
            Logger::info("Pairing completed with %s", session->info.name.c_str());

            NotificationPlugin::post_notification("kdeconnect", session->info.name,
                "Pair request accepted", session->info.id + "_accepted");
            return;
        }

        session->pair_state = PairState::RequestedByPeer;
        session->pairing_timestamp = static_cast<long>(timestamp);
        std::string key = verification_key(session, timestamp);
        Logger::info("Pair request from %s (key %s).", session->info.name.c_str(), key.c_str());
        Logger::info("Type: accept %s or reject %s", session->info.id.c_str(), session->info.id.c_str());

        NotificationPlugin::post_notification("kdeconnect", session->info.name,
            "Incoming pair request. Open the overlay to accept. Key: " + key, session->info.id + "_incoming");
    } else {
        auto previous_pair_state = session->pair_state;
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);

        if (previous_pair_state == PairState::Requested) {
            NotificationPlugin::post_notification("kdeconnect", session->info.name,
                "Pair request denied", session->info.id + "_denied");
            Logger::info("Pair request denied by %s", session->info.name.c_str());
        }
        else
            Logger::info("Unpaired from %s", session->info.name.c_str());
    }
}

std::string KdeConnectClient::verification_key(const std::shared_ptr<DeviceSession>& session, long timestamp) const {
    std::vector<unsigned char> a = tls_.local_pubkey_bytes();
    std::vector<unsigned char> b = session->peer_pubkey;
    if (a.empty() || b.empty()) {
        return "--------";
    }
    bool a_less_than_b = std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    std::vector<unsigned char> combined;
    if (a_less_than_b) {
        combined.insert(combined.end(), b.begin(), b.end());
        combined.insert(combined.end(), a.begin(), a.end());
    } else {
        combined.insert(combined.end(), a.begin(), a.end());
        combined.insert(combined.end(), b.begin(), b.end());
    }
    if (timestamp != 0) {
        std::string ts = std::to_string(timestamp);
        combined.insert(combined.end(), ts.begin(), ts.end());
    }
    return NetworkUtil::uppercase_first8(sha256_hex_upper(combined));
}

std::unordered_map<std::string, std::shared_ptr<KdeConnectClient::DeviceSession>> KdeConnectClient::devices() const {
    std::lock_guard lock(session_mutex_);
    return sessions_;
}

std::shared_ptr<KdeConnectClient::DeviceSession> KdeConnectClient::device(const std::string& device_id) const {
    std::lock_guard lock(session_mutex_);
    auto it = sessions_.find(device_id);
    if (it != sessions_.end()) {
        return it->second;
    }
    Logger::error("Device not found: %s", device_id.c_str());
    return nullptr;
}

void KdeConnectClient::request_pair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;

    if (session->paired) {
        Logger::warn("Already paired with %s", session->info.name.c_str());
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    long ts = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    pkt.body.set("pair", true).set("timestamp", (int64_t)ts);

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::Requested;
        session->pairing_timestamp = ts;
        std::string key = verification_key(session, ts);
        Logger::info("Pair request sent to %s (key %s).", session->info.name.c_str(), key.c_str());
    }
}

void KdeConnectClient::accept_pair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;

    if (session->cert_pem.empty()) {
        Logger::warn("Cannot accept pair for %s: missing peer certificate", session->info.id.c_str());
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body.set("pair", true)
            .set("timestamp", (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::Paired;
        session->paired = true;
        storage_.save_paired_device(session->info, session->cert_pem);
        Logger::info("Pair accepted for %s", session->info.name.c_str());
    }
}

void KdeConnectClient::reject_pair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body.set("pair", false);

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        Logger::info("Pair rejected for %s", session->info.name.c_str());
    }
}

void KdeConnectClient::unpair(const std::string& device_id) {
    Logger::info("Requesting unpair for %s", device_id.c_str());

    auto session = device(device_id);
    if (!session) {
        // Device is offline: remove from storage directly
        storage_.remove_paired_device(device_id);
        Logger::info("Unpaired offline device %s", device_id.c_str());
        return;
    }

    if (!session->paired) {
        Logger::warn("Device not paired: %s",
                 session->info.name.empty() ? device_id.c_str() : session->info.name.c_str());
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body.set("pair", false);

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        Logger::info("Unpaired from %s", session->info.name.c_str());
    }
}

std::vector<PairedDeviceInfo> KdeConnectClient::offline_paired_devices() const {
    const auto active = devices();
    std::vector<PairedDeviceInfo> offline;
    for (const auto& id : storage_.list_paired_device_ids()) {
        if (active.count(id)) continue;
        auto info = storage_.load_paired_device(id);
        if (info) offline.push_back(std::move(*info));
    }
    return offline;
}

bool KdeConnectClient::send_packet(const std::string& device_id, const NetworkPacket& pkt) {
    auto session = device(device_id);
    if (!session || session->disconnected.load()) return false;
    std::lock_guard lock(session->send_queue_mutex);
    session->send_queue.push(pkt.serialize());
    return true;
}

bool KdeConnectClient::send_payload(const std::string& device_id, NetworkPacket pkt) {
    if (pkt.payload.empty()) return false;

    ScopedFd srv_fd{socket(AF_INET, SOCK_STREAM, 0)};
    if (!srv_fd) return false;

    int reuse = 1;
    setsockopt(srv_fd.raw, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Find a free payload port in the standard KDE Connect payload range.
    int port = 0;
    sockaddr_in srv_addr{};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    for (int p = 1739; p <= 1764; ++p) {
        srv_addr.sin_port = htons(static_cast<uint16_t>(p));
        if (bind(srv_fd.raw, reinterpret_cast<sockaddr*>(&srv_addr), sizeof(srv_addr)) == 0) {
            port = p;
            break;
        }
    }
    if (port == 0 || listen(srv_fd.raw, 1) != 0) return false;

    pkt.payload_size = static_cast<int64_t>(pkt.payload.size());
    pkt.payload_port = port;

    if (!send_packet(device_id, pkt)) return false;

    auto payload = std::make_shared<std::vector<uint8_t>>(std::move(pkt.payload));
    auto done = std::make_shared<std::atomic<bool>>(false);
    const int raw_srv_fd = srv_fd.release();
    auto t = StackThread(16 * 1024, "kc-payload", [this, raw_srv_fd, payload, done]() mutable {
        ScopedFd srv_fd{raw_srv_fd};
        [&]() {
            // Give the receiver 15 s to connect after receiving the share packet.
            timeval timeout{15, 0};
            setsockopt(srv_fd.raw, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            ScopedFd client_fd{accept(srv_fd.raw, nullptr, nullptr)};
            if (!client_fd) return;

            // Receiver connects as TLS client; we are TLS server.
            auto tls_session = tls_.create_session(client_fd.raw, false);
            if (!tls_session || !NetworkUtil::perform_tls_handshake(*tls_session)) return;

            NetworkUtil::send_all_tls(*tls_session, *payload);
            mbedtls_ssl_close_notify(&tls_session->ssl);
        }();
        done->store(true);
    });
    std::lock_guard lock(pending_mutex_);
    pending_threads_.push_back({std::move(t), std::move(done)});

    return true;
}

void KdeConnectClient::io_loop(const std::shared_ptr<DeviceSession>& session) {
    // Drain outgoing queue. All TLS access is in this one thread, so reads and
    // writes never race on the mbedtls context. Returns false on send failure.
    auto drain_sends = [&]() -> bool {
        while (!session->disconnected.load()) {
            std::string payload;
            {
                std::lock_guard lock(session->send_queue_mutex);
                if (session->send_queue.empty()) return true;
                payload = std::move(session->send_queue.front());
                session->send_queue.pop();
            }
            if (!NetworkUtil::send_all_tls(*session->tls, payload))
                return false;
        }
        return false;
    };

    try {
        while (running_.load() && !session->disconnected.load()) {
            if (!drain_sends()) break;

            if (session->fd < 0) break;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(session->fd, &rfds);
            timeval tv{0, 5'000}; // 5 ms: max latency before queued sends are flushed
            int ret = select(session->fd + 1, &rfds, nullptr, nullptr, &tv);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) continue;

            auto line = NetworkUtil::read_line_tls(*session->tls, kMaxPacketSize);
            if (!line) break;
            handle_packet(session, *line);
        }
    } catch (const std::bad_alloc&) {
        Logger::error("Out of memory in io_loop for %s, disconnecting", session->info.name.c_str());
    }

    session->disconnected.store(true);
    if (session->fd >= 0) {
        shutdown(session->fd, SHUT_RDWR);
        close(session->fd);
        session->fd = -1;
    }
    if (session->tls) {
        mbedtls_ssl_close_notify(&session->tls->ssl);
        session->tls.reset();
    }
    malloc_trim(0);
    Logger::info("Disconnected from %s (%s)", session->info.name.c_str(), session->info.id.c_str());
}

