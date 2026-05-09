#include "notification_plugin.h"
#include "../utils/logger.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

static constexpr const char* kNotifyDir = "/config/ultrahand/notifications";
static constexpr const char* kIconPath  = "/config/ultrahand/assets/notifications/kdeconnect.rgba";
static constexpr const char* kAppId     = "kdeconnect";

static std::string sanitize_filename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out += (std::isalnum(c) || c == '-' || c == '_') ? static_cast<char>(c) : '_';
    }
    return out;
}

#ifdef __SWITCH__
static void write_icon() {
    static bool done = false;
    if (done) return;
    done = true;

    // Skip if icon already exists.
    FILE* probe = fopen(kIconPath, "rb");
    if (probe) { fclose(probe); return; }

    mkdir("/config/ultrahand", 0755);
    mkdir("/config/ultrahand/assets", 0755);
    mkdir("/config/ultrahand/assets/notifications", 0755);

    // 50x50 RGBA: KDE Connect blue (#1d99f3) background, white phone silhouette.
    static constexpr int W = 50, H = 50;
    uint8_t px[W * H * 4];

    auto set = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        int i = (y * W + x) * 4;
        px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = 0xff;
    };

    // Background
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            set(x, y, 0x1d, 0x99, 0xf3);

    // Phone body: white rounded rectangle (14–36, 7–43), 2px corner radius.
    for (int y = 7; y <= 43; y++) {
        for (int x = 14; x <= 36; x++) {
            if ((x < 16 && y < 9) || (x > 34 && y < 9) ||
                (x < 16 && y > 41) || (x > 34 && y > 41)) continue;
            set(x, y, 0xff, 0xff, 0xff);
        }
    }

    // Screen cutout: blue inset (17–33, 13–36).
    for (int y = 13; y <= 36; y++)
        for (int x = 17; x <= 33; x++)
            set(x, y, 0x1d, 0x99, 0xf3);

    // Earpiece: blue bar (21–29, 9–10).
    for (int y = 9; y <= 10; y++)
        for (int x = 21; x <= 29; x++)
            set(x, y, 0x1d, 0x99, 0xf3);

    // Home button: blue circle at (25, 40), radius 2.
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx*dx + dy*dy <= 4)
                set(25 + dx, 40 + dy, 0x1d, 0x99, 0xf3);

    FILE* f = fopen(kIconPath, "wb");
    if (f) {
        fwrite(px, 1, sizeof(px), f);
        fclose(f);
    } else {
        Logger::error("[NOTIFICATION] Failed to write icon: " + std::string(kIconPath));
    }
}
#endif

std::string NotificationPlugin::name() const { return "Notification Plugin"; }
std::string NotificationPlugin::description() const { return "Receives notifications from remote devices."; }

std::vector<std::string> NotificationPlugin::supported_packet_types() const {
    return { PacketTypes::Notification };
}

std::vector<std::string> NotificationPlugin::outgoing_packet_types() const {
    return { PacketTypes::NotificationRequest };
}

void NotificationPlugin::on_create() {
#ifdef __SWITCH__
    write_icon();
#endif
}

void NotificationPlugin::on_connected(bool paired) {
    if (paired) request_active_notifications();
}

bool NotificationPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::Notification) return false;

    if (np.body.value("isCancel", false)) {
        Logger::info("[NOTIFICATION] Dismissed: " + np.body.value("id", ""));
        return true;
    }

    bool silent      = np.body.value("silent", false);
    std::string app   = np.body.value("appName", "Unknown");
    std::string title = np.body.value("title", "");
    std::string text  = np.body.value("text", "");
    std::string id    = np.body.value("id", "");

    std::string log_msg = "[NOTIFICATION] " + app + ": " + title;
    if (!text.empty()) log_msg += " - " + text;
    Logger::info(log_msg);

    if (silent) return true;

    std::string body;
    if (!title.empty() && !text.empty())
        body = title + ": " + text;
    else if (!title.empty())
        body = title;
    else
        body = text;

    post_notification(app, body, id);
    return true;
}

void NotificationPlugin::post_notification(const std::string& title,
                                           const std::string& body,
                                           const std::string& id) {
#ifdef __SWITCH__
    mkdir(kNotifyDir, 0755);

    static std::atomic<int> s_counter{0};
    std::string uid = sanitize_filename(id);
    if (uid.empty()) uid = std::to_string(s_counter.fetch_add(1));

    std::string path     = std::string(kNotifyDir) + "/" + kAppId + "-" + uid + ".notify";
    std::string tmp_path = path + ".tmp";

    nlohmann::json payload = {
        {"title",      title},
        {"text",       body},
        {"duration",   4000},
        {"show_time",  "true"},
        {"split_type", "word"},
        {"alignment",  "left"},
    };

    std::ofstream f(tmp_path);
    if (f.is_open()) {
        f << payload.dump(2);
        f.close();
        rename(tmp_path.c_str(), path.c_str());
    } else {
        Logger::error("[NOTIFICATION] Failed to write notify file: " + tmp_path);
    }
#endif
    (void)title; (void)body; (void)id;
}

void NotificationPlugin::request_active_notifications() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::NotificationRequest;
    pkt.body = { {"request", true} };
    send_packet(pkt);
}
