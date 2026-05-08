#include "plugin_registry.h"
#include "ping_plugin.h"

namespace PluginRegistry {
    void instantiate_plugins(DeviceProvider* provider, const std::string& device_id, std::vector<std::unique_ptr<Plugin>>& plugins) {
        auto ping_plugin = std::make_unique<PingPlugin>();
        ping_plugin->init(provider, device_id);
        plugins.push_back(std::move(ping_plugin));
        
        // Add more plugins here in the future
    }

    static std::vector<std::string> caps;
    static std::vector<std::string> caps_out;

    void load_supported_types(DeviceProvider* provider) {
        if (caps.empty() && caps_out.empty()) {
            std::vector<std::unique_ptr<Plugin>> plugins;
            instantiate_plugins(provider, "dummy", plugins);
            for (const auto& p : plugins) {
                for (const auto& t : p->supported_packet_types()) {
                    caps.push_back(t);
                }
                for (const auto& t : p->outgoing_packet_types()) {
                    caps_out.push_back(t);
                }
            }
        }
    }

    std::vector<std::string> get_all_supported_packet_types(DeviceProvider* provider) {
        load_supported_types(provider);
        return caps;
    }

    std::vector<std::string> get_all_outgoing_packet_types(DeviceProvider* provider) {
        load_supported_types(provider);
        return caps_out;
    }
}
