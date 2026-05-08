#include "kdeconnect_client.h"

#include "network_packet.h"

#include <mbedtls/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>

namespace {
constexpr int kUdpPort = 1716;
constexpr int kMinTcpPort = 1716;
constexpr int kMaxTcpPort = 1764;
constexpr size_t kMaxPacketSize = 512 * 1024;
constexpr int kPairingWindowSeconds = 1800;

std::string now_string() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S");
    return oss.str();
}

void log_line(const std::string& level, const std::string& msg) {
    std::cerr << "[" << now_string() << "] " << level << ": " << msg << "\n";
}

std::optional<std::string> read_line_fd(int fd, size_t max_bytes) {
    std::string out;
    out.reserve(1024);
    char ch = 0;
    while (out.size() < max_bytes) {
        ssize_t read = recv(fd, &ch, 1, 0);
        if (read <= 0) {
            return std::nullopt;
        }
        if (ch == '\n') {
            return out;
        }
        out.push_back(ch);
    }
    return std::nullopt;
}

std::optional<std::string> read_line_tls(TlsSession& session, size_t max_bytes) {
    std::string out;
    out.reserve(1024);
    char ch = 0;
    while (out.size() < max_bytes) {
        int read = mbedtls_ssl_read(&session.ssl, reinterpret_cast<unsigned char*>(&ch), 1);
        if (read == MBEDTLS_ERR_SSL_WANT_READ || read == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (read <= 0) {
            return std::nullopt;
        }
        if (ch == '\n') {
            return out;
        }
        out.push_back(ch);
    }
    return std::nullopt;
}

bool send_all_tls(TlsSession& session, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        int written = mbedtls_ssl_write(&session.ssl,
                                        reinterpret_cast<const unsigned char*>(data.data() + total),
                                        data.size() - total);
        if (written == MBEDTLS_ERR_SSL_WANT_READ || written == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        total += static_cast<size_t>(written);
    }
    return true;
}

DeviceInfo info_from_identity(const NetworkPacket& pkt) {
    DeviceInfo info;
    info.id = pkt.body.value("deviceId", "");
    info.name = pkt.body.value("deviceName", "unknown");
    info.type = pkt.body.value("deviceType", "desktop");
    info.protocol_version = pkt.body.value("protocolVersion", kProtocolVersion);
    if (pkt.body.contains("incomingCapabilities")) {
        info.incoming_capabilities = pkt.body["incomingCapabilities"].get<std::vector<std::string>>();
    }
    if (pkt.body.contains("outgoingCapabilities")) {
        info.outgoing_capabilities = pkt.body["outgoingCapabilities"].get<std::vector<std::string>>();
    }
    return info;
}

NetworkPacket make_identity_packet(const DeviceInfo& info, std::optional<std::string> target_id,
                                   std::optional<int> target_protocol, std::optional<int> tcp_port) {
    NetworkPacket pkt;
    pkt.type = "kdeconnect.identity";
    pkt.body = nlohmann::json::object();
    pkt.body["deviceId"] = info.id;
    pkt.body["deviceName"] = info.name;
    pkt.body["deviceType"] = info.type;
    pkt.body["protocolVersion"] = info.protocol_version;
    pkt.body["incomingCapabilities"] = info.incoming_capabilities;
    pkt.body["outgoingCapabilities"] = info.outgoing_capabilities;
    if (target_id) {
        pkt.body["targetDeviceId"] = *target_id;
    }
    if (target_protocol) {
        pkt.body["targetProtocolVersion"] = *target_protocol;
    }
    if (tcp_port) {
        pkt.body["tcpPort"] = *tcp_port;
    }
    return pkt;
}

std::string uppercase_first8(const std::string& hex) {
    if (hex.size() <= 8) {
        return hex;
    }
    return hex.substr(0, 8);
}
} // namespace

struct KdeConnectClient::DeviceSession {
    DeviceInfo info;
    PairState pair_state = PairState::NotPaired;
    long pairing_timestamp = 0;
    bool paired = false;
    std::string cert_pem;
    std::vector<unsigned char> peer_pubkey;

    std::unique_ptr<TlsSession> tls;
    int fd = -1;
    std::thread reader;
    std::mutex send_mutex;
    std::atomic<bool> disconnected{false};
};

KdeConnectClient::KdeConnectClient(Storage storage) : storage_(std::move(storage)) {
    local_device_ = storage_.load_or_create_local_device();
}

KdeConnectClient::~KdeConnectClient() {
    stop();
}

bool KdeConnectClient::start() {
    if (running_.load()) {
        return true;
    }

    if (!tls_.load_or_create(storage_.cert_path(), storage_.key_path())) {
        log_line("ERROR", "Failed to load or create TLS identity.");
        return false;
    }
    local_device_.id = tls_.device_id();

    tcp_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd_ < 0) {
        log_line("ERROR", "Failed to create TCP socket.");
        return false;
    }
    int reuse = 1;
    setsockopt(tcp_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    bool bound = false;
    for (int port = kMinTcpPort; port <= kMaxTcpPort; ++port) {
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (bind(tcp_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            tcp_port_ = port;
            bound = true;
            break;
        }
    }
    if (!bound) {
        log_line("ERROR", "Failed to bind TCP socket on ports 1716-1764.");
        close(tcp_fd_);
        tcp_fd_ = -1;
        return false;
    }

    if (listen(tcp_fd_, 10) != 0) {
        log_line("ERROR", "Failed to listen on TCP socket.");
        close(tcp_fd_);
        tcp_fd_ = -1;
        return false;
    }

    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
        log_line("ERROR", "Failed to create UDP socket.");
        return false;
    }
    setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setsockopt(udp_fd_, SOL_SOCKET, SO_BROADCAST, &reuse, sizeof(reuse));

    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(kUdpPort);
    if (bind(udp_fd_, reinterpret_cast<sockaddr*>(&udp_addr), sizeof(udp_addr)) != 0) {
        log_line("WARN", "Unable to bind UDP socket for listening; discovery receive disabled.");
    }

    running_.store(true);
    tcp_thread_ = std::thread(&KdeConnectClient::tcp_accept_loop, this);
    udp_thread_ = std::thread(&KdeConnectClient::udp_listen_loop, this);
    broadcast_thread_ = std::thread(&KdeConnectClient::udp_broadcast_loop, this);

    log_line("INFO", "Listening on TCP port " + std::to_string(tcp_port_) + ".");
    return true;
}

void KdeConnectClient::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (tcp_fd_ >= 0) {
        close(tcp_fd_);
        tcp_fd_ = -1;
    }
    if (udp_fd_ >= 0) {
        close(udp_fd_);
        udp_fd_ = -1;
    }

    if (tcp_thread_.joinable()) {
        tcp_thread_.join();
    }
    if (udp_thread_.joinable()) {
        udp_thread_.join();
    }
    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();
    }

    std::lock_guard lock(session_mutex_);
    for (auto& [id, session] : sessions_) {
        if (session->fd >= 0) {
            shutdown(session->fd, SHUT_RDWR);
            close(session->fd);
            session->fd = -1;
        }
        if (session->reader.joinable()) {
            session->reader.join();
        }
        if (session->tls) {
            mbedtls_ssl_close_notify(&session->tls->ssl);
            session->tls.reset();
        }
    }
    sessions_.clear();
}

void KdeConnectClient::tcp_accept_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int fd = accept(tcp_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (fd < 0) {
            if (running_.load()) {
                log_line("WARN", "TCP accept failed.");
            }
            continue;
        }
        int keepalive = 1;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

        auto line = read_line_fd(fd, kMaxPacketSize);
        if (!line) {
            close(fd);
            continue;
        }
        auto packet = NetworkPacket::parse(*line);
        if (!packet || packet->type != "kdeconnect.identity") {
            close(fd);
            continue;
        }

        if (packet->body.contains("targetDeviceId")) {
            std::string target_id = packet->body.value("targetDeviceId", "");
            if (!target_id.empty() && target_id != local_device_.id) {
                close(fd);
                continue;
            }
        }
        if (packet->body.contains("targetProtocolVersion")) {
            int target_protocol = packet->body.value("targetProtocolVersion", kProtocolVersion);
            if (target_protocol != kProtocolVersion) {
                close(fd);
                continue;
            }
        }

        DeviceInfo identity = info_from_identity(*packet);
        handle_new_connection(identity, fd, true);
    }
}

void KdeConnectClient::udp_listen_loop() {
    while (running_.load()) {
        std::array<char, kMaxPacketSize> buffer{};
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        ssize_t len = recvfrom(udp_fd_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (len <= 0) {
            continue;
        }
        std::string data(buffer.data(), static_cast<size_t>(len));
        auto packet = NetworkPacket::parse(data);
        if (!packet || packet->type != "kdeconnect.identity") {
            continue;
        }
        DeviceInfo identity = info_from_identity(*packet);
        if (identity.id.empty() || identity.id == local_device_.id) {
            continue;
        }
        int tcp_port = packet->body.value("tcpPort", kMinTcpPort);
        char addr_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, addr_str, sizeof(addr_str));
        std::string address(addr_str);
        identity.protocol_version = packet->body.value("protocolVersion", kProtocolVersion);

        handle_discovered_peer(identity, address, tcp_port);
    }
}

void KdeConnectClient::udp_broadcast_loop() {
    int broadcast_count = 0;
    while (running_.load() && broadcast_count < 5) {
        NetworkPacket identity = make_identity_packet(local_device_, std::nullopt, std::nullopt, tcp_port_);
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
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return;
    }

    NetworkPacket my_identity = make_identity_packet(local_device_, identity.id, identity.protocol_version, std::nullopt);
    std::string payload = my_identity.serialize();
    send(fd, payload.data(), payload.size(), 0);

    handle_new_connection(identity, fd, false);
}

void KdeConnectClient::handle_new_connection(const DeviceInfo& identity, int fd, bool tcp_server_side) {
    if (identity.id.empty() || identity.id == local_device_.id) {
        close(fd);
        return;
    }

    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(identity.id);
        if (it != sessions_.end() && !it->second->disconnected.load()) {
            close(fd);
            return;
        }
    }

    auto stored = storage_.load_paired_device(identity.id);
    bool paired = stored.has_value();
    std::string stored_pem = stored ? stored->certificate_pem : std::string();

    auto tls_session = tls_.create_session(fd, tcp_server_side);
    if (!tls_session) {
        close(fd);
        return;
    }

    while (true) {
        int ret = mbedtls_ssl_handshake(&tls_session->ssl);
        if (ret == 0) {
            break;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            close(fd);
            return;
        }
    }

    std::string peer_pem = TlsContext::peer_cert_pem(*tls_session);
    if (paired && !stored_pem.empty() && peer_pem != stored_pem) {
        log_line("WARN", "Certificate mismatch for paired device " + identity.id + ", aborting connection");
        close(fd);
        return;
    }

    NetworkPacket secure_identity = make_identity_packet(local_device_, std::nullopt, std::nullopt, std::nullopt);
    if (!send_all_tls(*tls_session, secure_identity.serialize())) {
        close(fd);
        return;
    }

    auto line = read_line_tls(*tls_session, kMaxPacketSize);
    if (!line) {
        close(fd);
        return;
    }
    auto packet = NetworkPacket::parse(*line);
    if (!packet || packet->type != "kdeconnect.identity") {
        close(fd);
        return;
    }

    DeviceInfo secure_info = info_from_identity(*packet);
    if (secure_info.id != identity.id || secure_info.protocol_version != identity.protocol_version) {
        close(fd);
        return;
    }

    auto session = std::make_shared<DeviceSession>();
    session->info = secure_info;
    session->paired = paired;
    session->pair_state = paired ? PairState::Paired : PairState::NotPaired;
    session->tls = std::move(tls_session);
    session->fd = fd;
    session->cert_pem = peer_pem;
    session->peer_pubkey = tls_.peer_pubkey_bytes(*session->tls);

    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(session->info.id);
        if (it != sessions_.end()) {
            if (it->second->fd >= 0) {
                shutdown(it->second->fd, SHUT_RDWR);
                close(it->second->fd);
                it->second->fd = -1;
            }
            if (it->second->reader.joinable()) {
                it->second->reader.join();
            }
            if (it->second->tls) {
                mbedtls_ssl_close_notify(&it->second->tls->ssl);
                it->second->tls.reset();
            }
        }
        sessions_[session->info.id] = session;
    }

    log_line("INFO", "Connected to " + session->info.name + " (" + session->info.id + ")");

    session->reader = std::thread(&KdeConnectClient::read_loop, this, session);
}

void KdeConnectClient::read_loop(const std::shared_ptr<DeviceSession>& session) {
    while (running_.load()) {
        auto line = read_line_tls(*session->tls, kMaxPacketSize);
        if (!line) {
            break;
        }
        handle_packet(session, *line);
    }
    session->disconnected.store(true);
    log_line("INFO", "Disconnected from " + session->info.name + " (" + session->info.id + ")");
}

void KdeConnectClient::handle_packet(const std::shared_ptr<DeviceSession>& session, const std::string& line) {
    auto packet = NetworkPacket::parse(line);
    if (!packet) {
        return;
    }
    if (packet->type == "kdeconnect.pair") {
        handle_pair_packet(session, packet->body);
        return;
    }
    if (packet->type == "kdeconnect.ping") {
        handle_ping_packet(session, packet->body);
        return;
    }
}

void KdeConnectClient::handle_pair_packet(const std::shared_ptr<DeviceSession>& session, const nlohmann::json& body) {
    bool wants_pair = body.value("pair", false);
    if (wants_pair) {
        long timestamp = body.value("timestamp", 0L);
        long now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        if (timestamp != 0 && std::llabs(now - timestamp) > kPairingWindowSeconds) {
            log_line("WARN", "Pair request rejected due to clock skew.");
            return;
        }

        if (session->pair_state == PairState::Requested) {
            session->pair_state = PairState::Paired;
            session->paired = true;
            storage_.save_paired_device(session->info, session->cert_pem);
            log_line("INFO", "Pairing completed with " + session->info.name);
            return;
        }

        session->pair_state = PairState::RequestedByPeer;
        session->pairing_timestamp = timestamp;
        std::string key = verification_key(session, timestamp);
        log_line("INFO", "Pair request from " + session->info.name + " (key " + key + ").");
        log_line("INFO", "Type: accept " + session->info.id + " or reject " + session->info.id);
    } else {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        log_line("INFO", "Unpaired from " + session->info.name);
    }
}

void KdeConnectClient::handle_ping_packet(const std::shared_ptr<DeviceSession>& session, const nlohmann::json& body) {
    if (!session->paired) {
        return;
    }
    std::string message = body.value("message", "Ping!");
    log_line("PING", session->info.name + ": " + message);
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
    return uppercase_first8(sha256_hex_upper(combined));
}

void KdeConnectClient::list_devices() const {
    std::lock_guard lock(session_mutex_);
    if (sessions_.empty()) {
        std::cout << "No active devices.\n";
        return;
    }
    for (const auto& [id, session] : sessions_) {
        std::string state = session->paired ? "paired" : "unpaired";
        if (session->disconnected.load()) {
            state = "disconnected";
        }
        std::cout << session->info.name << " (" << id << ") - " << state << "\n";
    }
}

void KdeConnectClient::request_pair(const std::string& device_id) {
    std::shared_ptr<DeviceSession> session;
    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(device_id);
        if (it != sessions_.end()) {
            session = it->second;
        }
    }
    if (!session) {
        log_line("WARN", "Device not connected: " + device_id);
        return;
    }
    if (session->paired) {
        log_line("WARN", "Already paired with " + session->info.name);
        return;
    }

    NetworkPacket pkt;
    pkt.type = "kdeconnect.pair";
    pkt.body = nlohmann::json::object();
    pkt.body["pair"] = true;
    long ts = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    pkt.body["timestamp"] = ts;

    std::string payload = pkt.serialize();
    std::lock_guard lock(session->send_mutex);
    if (send_all_tls(*session->tls, payload)) {
        session->pair_state = PairState::Requested;
        session->pairing_timestamp = ts;
        std::string key = verification_key(session, ts);
        log_line("INFO", "Pair request sent to " + session->info.name + " (key " + key + ").");
    }
}

void KdeConnectClient::accept_pair(const std::string& device_id) {
    std::shared_ptr<DeviceSession> session;
    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(device_id);
        if (it != sessions_.end()) {
            session = it->second;
        }
    }
    if (!session) {
        log_line("WARN", "Device not connected: " + device_id);
        return;
    }

    NetworkPacket pkt;
    pkt.type = "kdeconnect.pair";
    pkt.body = nlohmann::json::object();
    pkt.body["pair"] = true;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    pkt.body["timestamp"] = now;

    std::string payload = pkt.serialize();
    std::lock_guard lock(session->send_mutex);
    if (send_all_tls(*session->tls, payload)) {
        session->pair_state = PairState::Paired;
        session->paired = true;
        storage_.save_paired_device(session->info, session->cert_pem);
        log_line("INFO", "Pair accepted for " + session->info.name);
    }
}

void KdeConnectClient::reject_pair(const std::string& device_id) {
    std::shared_ptr<DeviceSession> session;
    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(device_id);
        if (it != sessions_.end()) {
            session = it->second;
        }
    }
    if (!session) {
        log_line("WARN", "Device not connected: " + device_id);
        return;
    }

    NetworkPacket pkt;
    pkt.type = "kdeconnect.pair";
    pkt.body = nlohmann::json::object();
    pkt.body["pair"] = false;

    std::string payload = pkt.serialize();
    std::lock_guard lock(session->send_mutex);
    if (send_all_tls(*session->tls, payload)) {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        log_line("INFO", "Pair rejected for " + session->info.name);
    }
}

void KdeConnectClient::unpair(const std::string& device_id) {
    std::shared_ptr<DeviceSession> session;
    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(device_id);
        if (it != sessions_.end()) {
            session = it->second;
        }
    }
    if (!session) {
        log_line("WARN", "Device not connected: " + device_id);
        return;
    }
    if (!session->paired) {
        log_line("WARN", "Device not paired: " + (session->info.name.empty() ? device_id : session->info.name));
        return;
    }

    NetworkPacket pkt;
    pkt.type = "kdeconnect.pair";
    pkt.body = nlohmann::json::object();
    pkt.body["pair"] = false;

    std::string payload = pkt.serialize();
    std::lock_guard lock(session->send_mutex);
    if (send_all_tls(*session->tls, payload)) {
        session->pair_state = PairState::NotPaired;
        session->paired = false;
        storage_.remove_paired_device(session->info.id);
        log_line("INFO", "Unpaired from " + session->info.name);
    }
}

void KdeConnectClient::send_ping(const std::string& device_id, const std::string& message) {
    std::shared_ptr<DeviceSession> session;
    {
        std::lock_guard lock(session_mutex_);
        auto it = sessions_.find(device_id);
        if (it != sessions_.end()) {
            session = it->second;
        }
    }
    if (!session) {
        log_line("WARN", "Device not connected: " + device_id);
        return;
    }
    if (!session->paired) {
        log_line("WARN", "Device not paired: " + session->info.name);
        return;
    }

    NetworkPacket pkt;
    pkt.type = "kdeconnect.ping";
    pkt.body = nlohmann::json::object();
    if (!message.empty()) {
        pkt.body["message"] = message;
    }
    std::string payload = pkt.serialize();
    std::lock_guard lock(session->send_mutex);
    send_all_tls(*session->tls, payload);
}


