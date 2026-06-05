#include "run_command_plugin.h"
#include "share_plugin.h"
#include "utils/logger.h"
#include "utils/settings_store.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

// X(key, command_description, display_name, is_dangerous)
#define FOREACH_NX_COMMAND(X) \
    X("nx-auto-bright-off", "Disable auto brightness",                    "Auto Brightness Off", false) \
    X("nx-auto-bright-on",  "Enable auto brightness",                     "Auto Brightness On",  false) \
    X("nx-bluetooth-off",   "Turn Bluetooth radio off",                   "Bluetooth Off",       false) \
    X("nx-bluetooth-on",    "Turn Bluetooth radio on",                    "Bluetooth On",        false) \
    X("nx-bright-max",      "Set brightness to maximum strength",         "Brightness Max",      false) \
    X("nx-bright-min",      "Set brightness to minimum strength",         "Brightness Min",      false) \
    X("nx-reboot",          "Restart console",                            "Reboot",              true)  \
    X("nx-screen-off",      "Turn screen backlight off",                  "Screen Off",          false) \
    X("nx-screen-on",       "Turn screen backlight on",                   "Screen On",           false) \
    X("nx-screenshot",      "Take screenshot and send it to this device", "Receive screenshot",  false) \
    X("nx-shutdown",        "Turn off console",                           "Shutdown",            true)  \
    X("nx-sleep",           "Enter sleep mode (will disconnect)",         "Sleep",               true)

std::string RunCommandPlugin::name() const { return "Run Command Plugin"; }
std::string RunCommandPlugin::description() const { return "Exposes Switch system actions as remote commands."; }

std::vector<std::string> RunCommandPlugin::supported_packet_types() const {
    return { PacketTypes::RunCommandRequest, PacketTypes::RunCommand };
}

std::vector<std::string> RunCommandPlugin::outgoing_packet_types() const {
    return { PacketTypes::RunCommand, PacketTypes::RunCommandRequest };
}

void RunCommandPlugin::on_connected(const bool paired) {
    if (paired) {
        send_local_command_list();
        request_remote_command_list();
    }
}

bool RunCommandPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type == PacketTypes::RunCommand) {
        if (np.body.is_str("commandList")) {
            auto list = JsonBody::parse(np.body.get_str("commandList").c_str());
            std::lock_guard lock(remote_commands_mutex_);
            remote_commands_.clear();
            list.each_kv([&](const char* id, const JsonBody& entry) {
                if (entry.is_str("name"))
                    remote_commands_[id] = entry.get_str("name");
            });
        }
        return true;
    }

    if (np.type == PacketTypes::RunCommandRequest) {
        if (np.body.value("requestCommandList", false)) {
            send_local_command_list();
            return true;
        }
        if (np.body.is_str("key")) {
            run_local_command(np.body.get_str("key"));
            return true;
        }
    }

    return false;
}

void RunCommandPlugin::send_local_command_list() const {
    const bool power_enabled = SettingsStore::get(KdecBoolSettingKey::RunCommandPowerCommandsEnabled);

    std::string cmd_list = "{";
    bool first = true;
    const auto add = [&](const char* key, const char* cmd, const char* name, bool is_dangerous) {
        if (is_dangerous && !power_enabled) return;
        if (!first) cmd_list += ',';
        first = false;
        cmd_list += '"'; cmd_list += key;
        cmd_list += "\":{\"command\":\""; cmd_list += cmd;
        cmd_list += "\",\"name\":\"";     cmd_list += name;
        cmd_list += "\"}";
    };

#define X(key, cmd, name, dangerous) add(key, cmd, name, dangerous);
    FOREACH_NX_COMMAND(X)
#undef X

    cmd_list += '}';

    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommand;
    pkt.body.set("canAddCommand", false).set("commandList", cmd_list);
    send_packet(pkt);
}

void RunCommandPlugin::request_remote_command_list() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommandRequest;
    pkt.body.set("requestCommandList", true);
    send_packet(pkt);
}

std::vector<std::pair<std::string, std::string>> RunCommandPlugin::remote_command_list() const {
    std::lock_guard lock(remote_commands_mutex_);
    return { remote_commands_.begin(), remote_commands_.end() };
}

void RunCommandPlugin::run_remote_command(const std::string& key) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommandRequest;
    pkt.body.set("key", key);
    send_packet(pkt);
}

void RunCommandPlugin::run_local_command(const std::string& key) const {
    Logger::info("Executing command: %s", key.c_str());

    const bool is_dangerous =
#define X(k, c, n, d) (key == k && (d)) ||
        FOREACH_NX_COMMAND(X)
#undef X
        false;
    if (is_dangerous && !SettingsStore::get(KdecBoolSettingKey::RunCommandPowerCommandsEnabled))
        return;

#ifdef __SWITCH__
    // --- Power ---
    if (key == "nx-sleep") {
        appletRequestToSleep();
    } else if (key == "nx-reboot") {
        if (R_SUCCEEDED(bpcInitialize())) {
            bpcRebootSystem();
            bpcExit();
        }
    } else if (key == "nx-shutdown") {
        if (R_SUCCEEDED(bpcInitialize())) {
            bpcShutdownSystem();
            bpcExit();
        }

    // --- Screen ---
    } else if (key == "nx-screen-off") {
        if (R_SUCCEEDED(lblInitialize())) {
            lblSwitchBacklightOff(0);
            lblExit();
        }
    } else if (key == "nx-screen-on") {
        if (R_SUCCEEDED(lblInitialize())) {
            lblSwitchBacklightOn(0);
            lblExit();
        }
    } else if (key == "nx-bright-max") {
        if (R_SUCCEEDED(lblInitialize())) {
            lblSetCurrentBrightnessSetting(1.0f);
            lblApplyCurrentBrightnessSettingToBacklight();
            lblExit();
        }
    } else if (key == "nx-bright-min") {
        if (R_SUCCEEDED(lblInitialize())) {
            lblSetCurrentBrightnessSetting(0.0f);
            lblApplyCurrentBrightnessSettingToBacklight();
            lblExit();
        }
    } else if (key == "nx-auto-bright-on") {
        if (R_SUCCEEDED(lblInitialize())) {
            lblEnableAutoBrightnessControl();
            lblExit();
        }
    } else if (key == "nx-auto-bright-off") {
        if (R_SUCCEEDED(lblInitialize())) {
            lblDisableAutoBrightnessControl();
            lblExit();
        }

    // --- Bluetooth ---
    } else if (key == "nx-bluetooth-off") {
        if (R_SUCCEEDED(btmInitialize())) {
            btmDisableRadio();
            btmExit();
        }
    } else if (key == "nx-bluetooth-on") {
        if (R_SUCCEEDED(btmInitialize())) {
            btmEnableRadio();
            btmExit();
        }

    // --- Capture ---
    } else if (key == "nx-screenshot") {
        if (auto session = provider_->device(device_id_)) {
            if (auto* share = session->plugin<SharePlugin>())
                share->send_screenshot();
        }
    }
#endif
}
