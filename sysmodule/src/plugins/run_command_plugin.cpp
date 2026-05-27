#include "run_command_plugin.h"
#include "utils/logger.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

std::string RunCommandPlugin::name() const { return "Run Command Plugin"; }
std::string RunCommandPlugin::description() const { return "Exposes Switch system actions as remote commands."; }

std::vector<std::string> RunCommandPlugin::supported_packet_types() const {
    return { PacketTypes::RunCommandRequest, PacketTypes::RunCommand };
}

std::vector<std::string> RunCommandPlugin::outgoing_packet_types() const {
    return { PacketTypes::RunCommand, PacketTypes::RunCommandRequest };
}

void RunCommandPlugin::on_connected(bool paired) {
    if (paired) {
        send_local_command_list();
        request_remote_command_list();
    }
}

bool RunCommandPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type == PacketTypes::RunCommand) {
        if (np.body.contains("commandList") && np.body["commandList"].is_string()) {
            auto list = nlohmann::json::parse(np.body["commandList"].get<std::string>());
            std::lock_guard<std::mutex> lock(remote_commands_mutex_);
            remote_commands_.clear();
            for (auto& [id, entry] : list.items()) {
                if (entry.contains("name") && entry["name"].is_string())
                    remote_commands_[id] = entry["name"].get<std::string>();
            }
        }
        return true;
    }

    if (np.type == PacketTypes::RunCommandRequest) {
        if (np.body.value("requestCommandList", false)) {
            send_local_command_list();
            return true;
        }
        if (np.body.contains("key") && np.body["key"].is_string()) {
            run_local_command(np.body["key"].get<std::string>());
            return true;
        }
    }

    return false;
}

void RunCommandPlugin::send_local_command_list() const {
    static constexpr const char *kCommandList =
            R"({"canAddCommand":false,"commandList":")"
            R"({\"nx-auto-bright-off\":{\"command\":\"Disable auto brightness\",\"name\":\"Auto Brightness Off\"},)"
            R"(\"nx-auto-bright-on\":{\"command\":\"Enable auto brightness\",\"name\":\"Auto Brightness On\"},)"
            R"(\"nx-bluetooth-off\":{\"command\":\"Turn Bluetooth radio off\",\"name\":\"Bluetooth Off\"},)"
            R"(\"nx-bluetooth-on\":{\"command\":\"Turn Bluetooth radio on\",\"name\":\"Bluetooth On\"},)"
            R"(\"nx-bright-max\":{\"command\":\"Set brightness to maximum strength\",\"name\":\"Brightness Max\"},)"
            R"(\"nx-bright-min\":{\"command\":\"Set brightness to minimum strength\",\"name\":\"Brightness Min\"},)"
            R"(\"nx-reboot\":{\"command\":\"Restart console\",\"name\":\"Reboot\"},)"
            R"(\"nx-screen-off\":{\"command\":\"Turn screen backlight off\",\"name\":\"Screen Off\"},)"
            R"(\"nx-screen-on\":{\"command\":\"Turn screen backlight on\",\"name\":\"Screen On\"},)"
            R"(\"nx-screenshot\":{\"command\":\"Take screenshot\",\"name\":\"Screenshot\"},)"
            R"(\"nx-shutdown\":{\"command\":\"Turn off console\",\"name\":\"Shutdown\"},)"
            R"(\"nx-sleep\":{\"command\":\"Enter sleep mode (will disconnect)\",\"name\":\"Sleep\"}})"
            R"("})";

    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommand;
    pkt.body = nlohmann::json::parse(kCommandList);
    send_packet(pkt);
}

void RunCommandPlugin::request_remote_command_list() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommandRequest;
    pkt.body = nlohmann::json::parse(R"({"requestCommandList":true})");
    send_packet(pkt);
}

std::vector<std::pair<std::string, std::string>> RunCommandPlugin::remote_command_list() const {
    std::lock_guard lock(remote_commands_mutex_);
    return { remote_commands_.begin(), remote_commands_.end() };
}

void RunCommandPlugin::run_remote_command(const std::string& key) const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommandRequest;
    pkt.body = { {"key", key} };
    send_packet(pkt);
}

void RunCommandPlugin::run_local_command(const std::string& key) const {
    Logger::info("Executing command: %s", key.c_str());

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
        //appletSaveCurrentScreenshot(AlbumReportOption_Enable);
        if (R_SUCCEEDED(hidsysInitialize())) {
            hidsysActivateCaptureButton();
            hidsysExit();
        }
    }
#endif
}
