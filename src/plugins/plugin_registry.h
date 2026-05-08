#pragma once
#include <vector>
#include <memory>
#include <string>
#include "plugin.h"

namespace PluginRegistry {
    void instantiate_plugins(DeviceProvider* provider, const std::string& device_id, std::vector<std::unique_ptr<Plugin>>& plugins);
    
    std::vector<std::string> get_all_supported_packet_types(DeviceProvider* provider);
    std::vector<std::string> get_all_outgoing_packet_types(DeviceProvider* provider);
}
