#include "network_util.h"

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <mbedtls/error.h>
#include "utils/logger.h"

namespace NetworkUtil {

std::optional<std::string> read_line_fd(const int fd, const size_t max_bytes) {
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

std::optional<std::string> read_line_tls(TlsSession& session, const size_t max_bytes) {
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

static bool send_all_tls_raw(TlsSession& session, const unsigned char* data, const size_t len) {
    size_t total = 0;
    while (total < len) {
        int written = mbedtls_ssl_write(&session.ssl, data + total, len - total);
        if (written == MBEDTLS_ERR_SSL_WANT_READ || written == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (written <= 0) return false;
        total += static_cast<size_t>(written);
    }
    return true;
}

bool send_all_tls(TlsSession& session, const std::string& data) {
    return send_all_tls_raw(session,
        reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

bool send_all_tls(TlsSession& session, const std::vector<uint8_t>& data) {
    return send_all_tls_raw(session, data.data(), data.size());
}

bool send_all_tls(TlsSession& session, const unsigned char* data, const size_t len) {
    return send_all_tls_raw(session, data, len);
}

DeviceInfo info_from_identity(const NetworkPacket& pkt) {
    DeviceInfo info;
    info.id = pkt.body.value("deviceId", "");
    info.name = pkt.body.value("deviceName", "unknown");
    info.type = pkt.body.value("deviceType", "desktop");
    info.protocol_version = pkt.body.value("protocolVersion", kProtocolVersion);
    if (pkt.body.has("incomingCapabilities"))
        info.incoming_capabilities = pkt.body.get_str_array("incomingCapabilities");
    if (pkt.body.has("outgoingCapabilities"))
        info.outgoing_capabilities = pkt.body.get_str_array("outgoingCapabilities");
    return info;
}

NetworkPacket make_identity_packet(const DeviceInfo& info, const std::optional<std::string> &target_id,
                                   const std::optional<int> target_protocol, const std::optional<int> tcp_port) {
    NetworkPacket pkt;
    pkt.type = PacketTypes::Identity;
    pkt.body.set("deviceId",         info.id)
            .set("deviceName",       info.name)
            .set("deviceType",       info.type)
            .set("protocolVersion",  info.protocol_version)
            .set_str_array("incomingCapabilities", info.incoming_capabilities)
            .set_str_array("outgoingCapabilities", info.outgoing_capabilities);
    if (target_id)       pkt.body.set("targetDeviceId",          *target_id);
    if (target_protocol) pkt.body.set("targetProtocolVersion",   *target_protocol);
    if (tcp_port)        pkt.body.set("tcpPort",                 *tcp_port);
    return pkt;
}

std::string uppercase_first8(const std::string& hex) {
    if (hex.size() <= 8) {
        return hex;
    }
    return hex.substr(0, 8);
}

int create_tcp_server_socket(const int min_port, const int max_port, int& bound_port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    
    for (int port = min_port; port <= max_port; ++port) {
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            if (listen(fd, 10) == 0) {
                bound_port = port;
                return fd;
            }
        }
    }
    close(fd);
    return -1;
}

int create_udp_broadcast_socket(const int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool perform_tls_handshake(TlsSession& session) {
    while (true) {
        int ret = mbedtls_ssl_handshake(&session.ssl);
        if (ret == 0) return true;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            Logger::warn("SSL error: %s", errbuf);
            return false;
        }
    }
}

} // namespace NetworkUtil
