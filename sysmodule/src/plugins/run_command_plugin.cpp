#include "run_command_plugin.h"
#include "utils/logger.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

std::string RunCommandPlugin::name() const { return "Run Command Plugin"; }
std::string RunCommandPlugin::description() const { return "Exposes Switch system actions as remote commands."; }

std::vector<std::string> RunCommandPlugin::supported_packet_types() const {
    return { PacketTypes::RunCommandRequest };
}

std::vector<std::string> RunCommandPlugin::outgoing_packet_types() const {
    return { PacketTypes::RunCommand };
}

void RunCommandPlugin::on_connected(bool paired) {
    if (paired) send_command_list();
}

bool RunCommandPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::RunCommandRequest) return false;

    if (np.body.value("requestCommandList", false)) {
        send_command_list();
        return true;
    }

    if (np.body.contains("key") && np.body["key"].is_string()) {
        execute(np.body["key"].get<std::string>());
        return true;
    }

    return false;
}

void RunCommandPlugin::send_command_list() const {
    // commandList must be a serialized JSON string, not a nested object.
    nlohmann::json commands = {
        // Power
        {"nx-sleep",              {{"name", "Sleep"},                  {"command", "Enter sleep mode (will disconnect)"}}},
        {"nx-reboot",             {{"name", "Reboot"},                 {"command", "Restart console"}}},
        {"nx-shutdown",           {{"name", "Shutdown"},               {"command", "Turn off console"}}},
        // Screen
        {"nx-screen-off",         {{"name", "Screen Off"},             {"command", "Turn screen backlight off"}}},
        {"nx-screen-on",          {{"name", "Screen On"},              {"command", "Turn screen backlight on"}}},
        {"nx-bright-max",         {{"name", "Brightness Max"},         {"command", "Set brightness to maximum strength"}}},
        {"nx-bright-min",         {{"name", "Brightness Min"},         {"command", "Set brightness to minimum strength"}}},
        {"nx-auto-bright-on",     {{"name", "Auto Brightness On"},     {"command", "Enable auto brightness"}}},
        {"nx-auto-bright-off",    {{"name", "Auto Brightness Off"},    {"command", "Disable auto brightness"}}},
         // Bluetooth
        {"nx-bluetooth-off",      {{"name", "Bluetooth Off"},          {"command", "Turn Bluetooth radio off"}}},
        {"nx-bluetooth-on",       {{"name", "Bluetooth On"},           {"command", "Turn Bluetooth radio on"}}},
        // Capture
        {"nx-screenshot",         {{"name", "Screenshot"},             {"command", "Take screenshot"}}},
    };

    NetworkPacket pkt;
    pkt.type = PacketTypes::RunCommand;
    pkt.body = {
        {"commandList", commands.dump()},
        {"canAddCommand", false}
    };
    send_packet(pkt);
}

void RunCommandPlugin::execute(const std::string& key) const {
    Logger::info("Executing command: %s", key);

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
        appletSaveCurrentScreenshot(AlbumReportOption_Enable);
        /*if (R_SUCCEEDED(hidsysInitialize())) {
            hidsysActivateCaptureButton();
            hidsysExit();
        }*/
    }
#endif
}