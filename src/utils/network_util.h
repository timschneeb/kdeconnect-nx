#pragma once

#include <optional>
#include <string>

#include "../kdeconnect_types.h"
#include "../network_packet.h"
#include "../tls.h"

namespace NetworkUtil {

std::optional<std::string> read_line_fd(int fd, size_t max_bytes);
std::optional<std::string> read_line_tls(TlsSession& session, size_t max_bytes);
bool send_all_tls(TlsSession& session, const std::string& data);

DeviceInfo info_from_identity(const NetworkPacket& pkt);

NetworkPacket make_identity_packet(const DeviceInfo& info, std::optional<std::string> target_id,
                                   std::optional<int> target_protocol, std::optional<int> tcp_port);

std::string uppercase_first8(const std::string& hex);

int create_tcp_server_socket(int min_port, int max_port, int& bound_port);
int create_udp_broadcast_socket(int port);
bool perform_tls_handshake(TlsSession& session);

} // namespace NetworkUtil
