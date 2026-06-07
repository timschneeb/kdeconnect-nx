#include "mousepad_plugin.h"

#include "notification_plugin.h"
#include "utils/logger.h"
#include "hiddbg_util.h"

static std::atomic s_no_mouse_support_hint_shown{false};

#ifdef __SWITCH__
#include <switch.h>
#include <atomic>

struct HidKeyInfo {
    uint8_t hid_code;
    bool needs_shift;
};

// Maps an ASCII byte to a HID keyboard usage ID (EN_US) and whether Shift is needed.
static HidKeyInfo char_to_hid(const unsigned char c) {
    if (c >= 'a' && c <= 'z') return {static_cast<uint8_t>(0x04u + (c - 'a')), false};
    if (c >= 'A' && c <= 'Z') return {static_cast<uint8_t>(0x04u + (c - 'A')), true};
    if (c >= '1' && c <= '9') return {static_cast<uint8_t>(0x1Eu + (c - '1')), false};
    switch (c) {
        case '0': return {0x27, false};
        case '\n': return {0x28, false};
        case '\r': return {0x28, false};
        case '\t': return {0x2B, false};
        case ' ': return {0x2C, false};
        case '-': return {0x2D, false};
        case '_': return {0x2D, true};
        case '=': return {0x2E, false};
        case '+': return {0x2E, true};
        case '[': return {0x2F, false};
        case '{': return {0x2F, true};
        case ']': return {0x30, false};
        case '}': return {0x30, true};
        case '\\': return {0x31, false};
        case '|': return {0x31, true};
        case ';': return {0x33, false};
        case ':': return {0x33, true};
        case '\'': return {0x34, false};
        case '"': return {0x34, true};
        case '`': return {0x35, false};
        case '~': return {0x35, true};
        case ',': return {0x36, false};
        case '<': return {0x36, true};
        case '.': return {0x37, false};
        case '>': return {0x37, true};
        case '/': return {0x38, false};
        case '?': return {0x38, true};
        case '!': return {0x1E, true};
        case '@': return {0x1F, true};
        case '#': return {0x20, true};
        case '$': return {0x21, true};
        case '%': return {0x22, true};
        case '^': return {0x23, true};
        case '&': return {0x24, true};
        case '*': return {0x25, true};
        case '(': return {0x26, true};
        case ')': return {0x27, true};
        default: return {0, false};
    }
}

// Maps KDE Connect special key IDs to HID keyboard usage IDs.
static constexpr uint8_t kSpecialKeyMap[] = {
    0x00, // 0  undefined
    0x2A, // 1  Backspace
    0x2B, // 2  Tab
    0x00, // 3  unused
    0x50, // 4  Left
    0x52, // 5  Up
    0x4F, // 6  Right
    0x51, // 7  Down
    0x4B, // 8  Page Up
    0x4E, // 9  Page Down
    0x4A, // 10 Home
    0x4D, // 11 End
    0x28, // 12 Return
    0x4C, // 13 Delete
    0x29, // 14 Escape
    0x46, // 15 SysRq/PrintScreen
    0x47, // 16 ScrollLock
    0x00, // 17 unused
    0x00, // 18 unused
    0x00, // 19 unused
    0x00, // 20 unused
    0x3A, // 21 F1
    0x3B, // 22 F2
    0x3C, // 23 F3
    0x3D, // 24 F4
    0x3E, // 25 F5
    0x3F, // 26 F6
    0x40, // 27 F7
    0x41, // 28 F8
    0x42, // 29 F9
    0x43, // 30 F10
    0x44, // 31 F11
    0x45, // 32 F12
};

// Modifier keys need to be injected as HID key bits (usage IDs 0xE0-0xE3 in keys[3]),
// not via the modifiers field which only reflects lock-key state.
static void do_inject_hid(const uint8_t hid_code, const bool shift, const bool ctrl = false, const bool alt = false, const bool gui = false,
                          const bool altgr = false) {
    HiddbgKeyboardAutoPilotState state{};
    state.keys[hid_code >> 6] |= (1ULL << (hid_code & 63));
    if (ctrl) state.keys[0xE0 >> 6] |= (1ULL << (0xE0 & 63)); // Left Control
    if (shift) state.keys[0xE1 >> 6] |= (1ULL << (0xE1 & 63)); // Left Shift
    if (alt) state.keys[0xE2 >> 6] |= (1ULL << (0xE2 & 63)); // Left Alt
    if (gui) state.keys[0xE3 >> 6] |= (1ULL << (0xE3 & 63)); // Left GUI/Super
    if (altgr) state.keys[0xE5 >> 6] |= (1ULL << (0xE5 & 63)); // Right Alt / AltGr

    hiddbgSetKeyboardAutoPilotState(&state);
    svcSleepThread(25'000'000LL); // 25 ms key hold

    // Dummy press allows back-to-back identical keys to register.
    HiddbgKeyboardAutoPilotState dummy{};
    dummy.keys[3] = 0x800000000000000ULL;
    hiddbgSetKeyboardAutoPilotState(&dummy);
    svcSleepThread(17'000'000LL*2); // ~2 frame @ 60 fps

    hiddbgUnsetKeyboardAutoPilotState();
}

#endif // __SWITCH__

std::string MousepadPlugin::name() const { return "Mousepad Plugin"; }
std::string MousepadPlugin::description() const { return "Receives keyboard input from remote devices."; }

std::vector<std::string> MousepadPlugin::supported_packet_types() const {
    return {PacketTypes::MousepadRequest};
}

std::vector<std::string> MousepadPlugin::outgoing_packet_types() const {
    return {PacketTypes::MousepadEcho, PacketTypes::MousepadKeyboardState};
}


void MousepadPlugin::on_create() {
#ifdef __SWITCH__
    hiddbg_retain();
#endif
}

void MousepadPlugin::on_destroy() {
#ifdef __SWITCH__
    hiddbg_release();
#endif
}

void MousepadPlugin::on_connected(const bool paired) {
    if (paired) {
        send_keyboard_state();
    }
}

void MousepadPlugin::send_keyboard_state() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::MousepadKeyboardState;
    pkt.body.set("state", true);
    send_packet(pkt);
}

void MousepadPlugin::send_echo(const NetworkPacket &np) const {
    NetworkPacket echo;
    echo.type = PacketTypes::MousepadEcho;
    for (const char *f: {
             "key", "specialKey", "alt", "ctrl", "shift", "super",
             "singleclick", "doubleclick", "middleclick", "rightclick",
             "singlehold", "singlerelease", "dx", "dy", "scroll"
         }) {
        echo.body.copy_field(f, np.body);
    }
    echo.body.set("isAck", true);
    send_packet(echo);
}

bool MousepadPlugin::on_packet_received(const NetworkPacket &np) {
    if (np.type != PacketTypes::MousepadRequest) return false;

    if (np.body.has("dx") || np.body.has("dy")) {
        bool expected = false;
        if (s_no_mouse_support_hint_shown.compare_exchange_strong(expected, true)) {
            NotificationPlugin::post_notification("mousepad",
                                        "Mouse support is not available",
                                                "You just tried to use the remote touchpad/mouse. Only remote keyboard events are supported.",
                                                "0",
                                                8000);
        }
    }

    inject_key(np);
    if (np.body.value("sendAck", false)) send_echo(np);
    return true;
}

void MousepadPlugin::inject_key(const NetworkPacket &np) {
#ifdef __SWITCH__
    if (!hiddbg_is_available()) {
        Logger::error("inject_key: hiddbg not initialized");
        return;
    }

    setsysInitialize();
    SetKeyboardLayout old_layout;
    setsysGetKeyboardLayout(&old_layout);
    // We assume the US English keyboard scan code mapping
    setsysSetKeyboardLayout(SetKeyboardLayout_EnglishUs);

    bool alt = np.body.value("alt", false);
    bool ctrl = np.body.value("ctrl", false);
    bool shift = np.body.value("shift", false);
    bool super = np.body.value("super", false);

    auto do_key = [&](const uint8_t hid_code, const bool needs_shift, const bool needs_altgr = false) {
        bool apply_shift = shift || needs_shift;
#ifdef DEBUG
        std::string info = [&] {
            char buf[40];
            snprintf(buf, sizeof(buf), "%02X shift=%d ctrl=%d alt=%d gui=%d altgr=%d",
                     hid_code, apply_shift, ctrl, alt, super, needs_altgr);
            return std::string(buf);
        }();
        Logger::info("Key inject: hid=0x%s", info.c_str());
#endif
        do_inject_hid(hid_code, apply_shift, ctrl, alt, super, needs_altgr);
    };

    if (np.body.is_num("specialKey") && np.body.get_int("specialKey") != 0) {
        int special = np.body.get_int("specialKey");
        if (special > 0 && special < static_cast<int>(sizeof(kSpecialKeyMap))) {
            uint8_t hid_code = kSpecialKeyMap[special];
            if (hid_code != 0) do_key(hid_code, false);
        }
    } else if (np.body.is_str("key") && !np.body.get_str("key").empty()) {
        for (unsigned char c: np.body.get_str("key")) {
            if (c >= 0x80) continue; // skip non-ASCII / UTF-8 continuation bytes

            auto [hid_code, needs_shift] = char_to_hid(c);
            if (hid_code != 0) do_key(hid_code, needs_shift, false);
        }
    }

    if (old_layout != SetKeyboardLayout_EnglishUs) {
        setsysSetKeyboardLayout(old_layout);
    }
    setsysExit();
#else
    Logger::info("Key event received: %s", np.body.dump().c_str());
#endif
}
