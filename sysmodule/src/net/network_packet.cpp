#include "network_packet.h"

#include <chrono>

std::string NetworkPacket::serialize() const {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    JsonBody root;
    root.set("id",   (int64_t)now)
        .set("type", type)
        .set("body", body);

    if (payload_port >= 0 && payload_size > 0) {
        root.set("payloadSize", payload_size);
        JsonBody ti;
        ti.set("port", payload_port);
        root.set("payloadTransferInfo", std::move(ti));
    }
    return root.dump() + "\n";
}

std::optional<NetworkPacket> NetworkPacket::parse(const std::string& line) {
    auto root = JsonBody::parse(line.c_str());
    if (!root.has("type") || !root.has("body")) return std::nullopt;

    NetworkPacket pkt;
    pkt.type = root.get_str("type");
    pkt.body = root.get_obj("body");

    if (root.is_num("payloadSize")) {
        pkt.payload_size = root.get_i64("payloadSize");
        if (root.is_obj("payloadTransferInfo")) {
            auto ti = root.get_obj("payloadTransferInfo");
            pkt.payload_port = ti.value("port", -1);
        }
    }
    return pkt;
}
