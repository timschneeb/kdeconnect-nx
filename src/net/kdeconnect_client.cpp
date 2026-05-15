#include "kdeconnect_client.h"

#include "../network_packet.h"

#include <mbedtls/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <sys/select.h>
#include <thread>

#include "../plugins/plugin_registry.h"

#include "../utils/logger.h"
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

void configure_tcp_keepalive(int fd) {
    int keepalive = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &kKeepaliveIdleSeconds, sizeof(kKeepaliveIdleSeconds));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &kKeepaliveIntervalSeconds, sizeof(kKeepaliveIntervalSeconds));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &kKeepaliveProbeCount, sizeof(kKeepaliveProbeCount));
}

} // namespace

KdeConnectClient::KdeConnectClient(Storage storage) : storage_(std::move(storage)) {
    local_device_ = storage_.load_or_create_local_device(this);
}

KdeConnectClient::~KdeConnectClient() {
    stop();
}

bool KdeConnectClient::start() {
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

    mdns_discovery_ = std::make_unique<MdnsDiscovery>(local_device_, tcp_port_, [this](const std::string& device_id, const std::string& host) {
        try {
            {
                std::lock_guard lock(session_mutex_);
                if (sessions_.contains(device_id) && !sessions_[device_id]->disconnected.load()) {
                    // already connected
                    return;
                }
            }

            Logger::info("mDNS: Sending probe to " + device_id + " at " + host);
            send_udp_identity_probe(device_id, host);
        } catch (const std::exception &e) {
            Logger::error("Error handling mDNS discovery for device " + device_id + " at " + host + ": " + e.what());
        }
    });

    needs_restart_.store(false);
    running_.store(true);
    network_thread_ = std::thread(&KdeConnectClient::network_loop, this);
    broadcast_thread_ = std::thread(&KdeConnectClient::udp_broadcast_loop, this);

    Logger::info("Listening on TCP port " + std::to_string(tcp_port_) + ".");

    if (!mdns_discovery_->start()) {
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

    if (tcp_fd_ >= 0) {
        close(tcp_fd_);
        tcp_fd_ = -1;
    }
    if (udp_fd_ >= 0) {
        close(udp_fd_);
        udp_fd_ = -1;
    }

    if (network_thread_.joinable()) {
        network_thread_.join();
    }
    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();
    }

    std::vector<std::shared_ptr<DeviceSession>> to_stop;
    {
        std::lock_guard lock(session_mutex_);
        for (auto &session: sessions_ | std::views::values) {
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
        Logger::warn("Ignoring mDNS peer with invalid IPv4 address: " + host);
        return;
    }

    sendto(udp_fd_, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

void KdeConnectClient::network_loop() {
    while (running_.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int nfds = 0;
        if (tcp_fd_ >= 0) { FD_SET(tcp_fd_, &rfds); nfds = std::max(nfds, tcp_fd_ + 1); }
        if (udp_fd_ >= 0) { FD_SET(udp_fd_, &rfds); nfds = std::max(nfds, udp_fd_ + 1); }
        if (nfds == 0) break;

        // Use a timeout so stop() can set running_=false and have this thread
        // exit within ~500 ms rather than blocking forever in select().
        timeval tv{0, 500'000};
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
                Logger::warn("TCP accept failed (" + std::string(strerror(errno)) + ")");
                needs_restart_.store(true);
                break;
            } else {
                configure_tcp_keepalive(fd.raw);
                auto line = NetworkUtil::read_line_fd(fd.raw, kMaxPacketSize);
                auto packet = line ? NetworkPacket::parse(*line) : std::optional<NetworkPacket>{};
                bool valid = packet && packet->type == PacketTypes::Identity;
                if (valid && packet->body.contains("targetDeviceId")) {
                    const auto tid = packet->body.value("targetDeviceId", "");
                    if (!tid.empty() && tid != local_device_.id) valid = false;
                }
                if (valid && packet->body.contains("targetProtocolVersion")) {
                    if (packet->body.value("targetProtocolVersion", kProtocolVersion) != kProtocolVersion)
                        valid = false;
                }
                if (valid)
                    handle_new_connection(NetworkUtil::info_from_identity(*packet), std::move(fd), true);
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
        std::this_thread::sleep_for(std::chrono::seconds(1));
        broadcast_count++;
    }
}

void KdeConnectClient::handle_discovered_peer(const DeviceInfo& identity, const std::string& host, int port) {
    ScopedFd fd{socket(AF_INET, SOCK_STREAM, 0)};
    if (!fd) return;
    configure_tcp_keepalive(fd.raw);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) return;
    if (connect(fd.raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return;

    NetworkPacket my_identity = NetworkUtil::make_identity_packet(local_device_, identity.id, identity.protocol_version, std::nullopt);
    std::string payload = my_identity.serialize();
    send(fd.raw, payload.data(), payload.size(), 0);

    handle_new_connection(identity, std::move(fd), false);
}

void KdeConnectClient::handle_new_connection(const DeviceInfo& identity, ScopedFd fd, bool tcp_server_side) {
    if (identity.id.empty() || identity.id == local_device_.id) return;

    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(identity.id);
        if (it != sessions_.end() && !it->second->disconnected.load()) {
            // TODO: allow old but alive connections to be replaced?
            Logger::warn("Already connected to " + identity.name + " (" + identity.id + ") but a new connection was received.");
            return;
        }
    }

    auto stored = storage_.load_paired_device(identity.id);
    bool paired = stored.has_value();
    std::string stored_pem = stored ? stored->certificate_pem : std::string();

    auto tls_session = tls_.create_session(fd.raw, tcp_server_side);
    if (!tls_session) {
        Logger::error("Failed to create TLS session for device " + identity.id + (tcp_server_side ? " (server-side)" : " (client-side)"));
        return;
    }

    if (!NetworkUtil::perform_tls_handshake(*tls_session)) {
        Logger::error("TLS handshake failed for device " + identity.id + (tcp_server_side ? " (server-side)" : " (client-side)"));
        return;
    }

    std::string peer_pem = TlsContext::peer_cert_pem(*tls_session);
    if (peer_pem.empty() && tcp_server_side) {
        Logger::warn("Peer did not present a TLS certificate for device " + identity.id + (tcp_server_side ? " (server-side)" : " (client-side)"));
        return;
    }

    if (paired && !stored_pem.empty() && peer_pem != stored_pem && tcp_server_side) {
        Logger::warn("Certificate mismatch for paired device " + identity.id + ", aborting connection");
        return;
    }

    NetworkPacket secure_identity = NetworkUtil::make_identity_packet(local_device_, std::nullopt, std::nullopt, std::nullopt);
    if (!NetworkUtil::send_all_tls(*tls_session, secure_identity.serialize())) {
        Logger::error("Failed to send identity packet to " + identity.name + " (" + identity.id + ") after TLS handshake.");
        return;
    }

    auto line = NetworkUtil::read_line_tls(*tls_session, kMaxPacketSize);
    if (!line) {
        Logger::error("Failed to read data from " + identity.name + " (" + identity.id + ") after TLS handshake.");
        return;
    }
    auto packet = NetworkPacket::parse(*line);
    if (!packet || packet->type != PacketTypes::Identity) {
        Logger::error("Failed to parse packet from " + identity.name + " (" + identity.id + ") after TLS handshake.");
        return;
    }

    DeviceInfo secure_info = NetworkUtil::info_from_identity(*packet);
    if (secure_info.id != identity.id || secure_info.protocol_version != identity.protocol_version) {
        Logger::error("Identity mismatch for " + identity.name + " (" + identity.id + ") after TLS handshake.");
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
    }

    Logger::info("Connected to " + session->info.name + " (" + session->info.id + ", " + (tcp_server_side ? "server-side" : "client-side") + ", " + (session->paired ? "paired" : "unpaired") + ").");

    session->io_thread = std::thread(&KdeConnectClient::io_loop, this, session);

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

    if (packet->payload_port > 0 && packet->payload_size > 0 && !session->peer_host.empty())
        download_payload(session, *packet);

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

void KdeConnectClient::download_payload(const std::shared_ptr<DeviceSession>& session, NetworkPacket& packet) {
    ScopedFd fd{socket(AF_INET, SOCK_STREAM, 0)};
    if (!fd) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(packet.payload_port));
    if (inet_pton(AF_INET, session->peer_host.c_str(), &addr.sin_addr) != 1) return;
    if (connect(fd.raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return;

    auto tls_session = tls_.create_session(fd.raw, true);
    if (!tls_session || !NetworkUtil::perform_tls_handshake(*tls_session)) return;

    const int64_t cap = std::min(packet.payload_size, static_cast<int64_t>(10 * 1024 * 1024));
    packet.payload.resize(static_cast<size_t>(cap));

    size_t received = 0;
    while (received < static_cast<size_t>(cap)) {
        int ret = mbedtls_ssl_read(&tls_session->ssl,
                                   packet.payload.data() + received,
                                   static_cast<size_t>(cap) - received);
        if (ret <= 0) break;
        received += static_cast<size_t>(ret);
    }
    packet.payload.resize(received);

    mbedtls_ssl_close_notify(&tls_session->ssl);

    Logger::info("Downloaded payload: " + std::to_string(received) + " bytes for " + packet.type);
}

void KdeConnectClient::handle_pair_packet(const std::shared_ptr<DeviceSession>& session, const nlohmann::json& body) {
    bool wants_pair = body.value("pair", false);
    if (wants_pair) {
        long timestamp = body.value("timestamp", 0L);
        long now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        if (timestamp != 0 && std::llabs(now - timestamp) > kPairingWindowSeconds) {
            Logger::warn("Pair request rejected due to clock skew.");
            return;
        }

        if (session->pair_state == PairState::Requested) {
            if (session->cert_pem.empty()) {
                Logger::warn("Pairing failed for " + session->info.name + ": missing peer certificate");
                return;
            }
            session->pair_state = PairState::Paired;
            session->paired = true;
            storage_.save_paired_device(session->info, session->cert_pem);
            Logger::info("Pairing completed with " + session->info.name);
            return;
        }

        session->pair_state = PairState::RequestedByPeer;
        session->pairing_timestamp = timestamp;
        std::string key = verification_key(session, timestamp);
        Logger::info("Pair request from " + session->info.name + " (key " + key + ").");
        Logger::info("Type: accept " + session->info.id + " or reject " + session->info.id);
    } else {
        auto previous_pair_state = session->pair_state;
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);

        if (previous_pair_state == PairState::Requested)
            Logger::info("Pair request denied by " + session->info.name);
        else
            Logger::info("Unpaired from " + session->info.name);
    }
}

std::string KdeConnectClient::verification_key(const std::shared_ptr<DeviceSession>& session, long timestamp) const {
    std::vector<unsigned char> a = tls_.local_pubkey_bytes();
    std::vector<unsigned char> b = session->peer_pubkey;
    if (a.empty() || b.empty()) {
        return "--------";
    }
    bool a_less_than_b = std::ranges::lexicographical_compare(a, b);
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
    Logger::error("Device not found: " + device_id);
    return nullptr;
}

void KdeConnectClient::request_pair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;
    
    if (session->paired) {
        Logger::warn("Already paired with " + session->info.name);
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body = {{"pair", true}};
    long ts = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    pkt.body["timestamp"] = ts;

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::Requested;
        session->pairing_timestamp = ts;
        std::string key = verification_key(session, ts);
        Logger::info("Pair request sent to " + session->info.name + " (key " + key + ").");
    }
}

void KdeConnectClient::accept_pair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;

    if (session->cert_pem.empty()) {
        Logger::warn("Cannot accept pair for " + session->info.name + ": missing peer certificate");
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body = {
        {"pair", true},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::Paired;
        session->paired = true;
        storage_.save_paired_device(session->info, session->cert_pem);
        Logger::info("Pair accepted for " + session->info.name);
    }
}

void KdeConnectClient::reject_pair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body = {{"pair", false}};

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        Logger::info("Pair rejected for " + session->info.name);
    }
}

void KdeConnectClient::unpair(const std::string& device_id) {
    auto session = device(device_id);
    if (!session) return;
    
    if (!session->paired) {
        Logger::warn("Device not paired: " + (session->info.name.empty() ? device_id : session->info.name));
        return;
    }

    NetworkPacket pkt;
    pkt.type = PacketTypes::Pair;
    pkt.body = {{"pair", false}};

    if (send_packet(device_id, pkt)) {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        Logger::info("Unpaired from " + session->info.name);
    }
}

bool KdeConnectClient::send_packet(const std::string& device_id, const NetworkPacket& pkt) {
    auto session = device(device_id);
    if (!session || session->disconnected.load()) return false;
    std::lock_guard lock(session->send_queue_mutex);
    session->send_queue.push(pkt.serialize());
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
    Logger::info("Disconnected from " + session->info.name + " (" + session->info.id + ")");
}

