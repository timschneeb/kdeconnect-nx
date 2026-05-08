#include "network_util.h"

#include <sys/socket.h>

namespace NetworkUtil {

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
    pkt.type = PacketTypes::Identity;
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

} // namespace NetworkUtil
