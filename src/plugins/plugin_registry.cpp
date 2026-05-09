#include "plugin_registry.h"
#include "ping_plugin.h"
#include "battery_plugin.h"
#include "notification_plugin.h"
#include "find_my_phone_plugin.h"
#include "mpris_plugin.h"
#include "system_volume_plugin.h"
#include "share_plugin.h"
#include "run_command_plugin.h"

namespace PluginRegistry {
    void instantiate_plugins(DeviceProvider* provider, const std::string& device_id, std::vector<std::unique_ptr<Plugin>>& plugins) {
        auto add = [&](auto plugin) {
            plugin->init(provider, device_id);
            plugins.push_back(std::move(plugin));
        };
        add(std::make_unique<PingPlugin>());
        add(std::make_unique<BatteryPlugin>());
        add(std::make_unique<NotificationPlugin>());
        add(std::make_unique<FindMyPhonePlugin>());
        add(std::make_unique<MprisPlugin>());
        add(std::make_unique<SystemVolumePlugin>());
        add(std::make_unique<SharePlugin>());
        add(std::make_unique<RunCommandPlugin>());
    }

    static std::vector<std::string> caps;
    static std::vector<std::string> caps_out;

    void load_supported_types(DeviceProvider* provider) {
        if (caps.empty() && caps_out.empty()) {
            std::vector<std::unique_ptr<Plugin>> plugins;
            instantiate_plugins(provider, "dummy", plugins);
            for (const auto& p : plugins) {
                for (const auto& t : p->supported_packet_types()) caps.push_back(t);
                for (const auto& t : p->outgoing_packet_types()) caps_out.push_back(t);
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
