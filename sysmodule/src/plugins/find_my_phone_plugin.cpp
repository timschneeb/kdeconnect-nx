#include "find_my_phone_plugin.h"
#include "utils/logger.h"

std::string FindMyPhonePlugin::name() const { return "Find My Phone Plugin"; }
std::string FindMyPhonePlugin::description() const { return "Sends find request to remote devices."; }

std::vector<std::string> FindMyPhonePlugin::supported_packet_types() const {
    return {};
}

std::vector<std::string> FindMyPhonePlugin::outgoing_packet_types() const {
    return { PacketTypes::FindMyPhoneRequest };
}

void FindMyPhonePlugin::find() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::FindMyPhoneRequest;
    pkt.body = nlohmann::json::object();
    Logger::info("Sending find request to %s", device_id_.c_str());
    send_packet(pkt);
}
