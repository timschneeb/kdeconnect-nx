#include "notification_plugin.h"
#include "../utils/logger.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#ifdef __SWITCH__
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#include <dirent.h>
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
static void clear_our_icons() {
    static constexpr const char* kIconDir = "/config/ultrahand/assets/notifications";
    DIR* d = opendir(kIconDir);
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.rfind("kdeconnect", 0) == 0 && name.size() > 5 &&
                name.compare(name.size() - 5, 5, ".rgba") == 0) {
            remove((std::string(kIconDir) + "/" + name).c_str());
        }
    }
    closedir(d);
}

static void write_icon() {
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
    clear_our_icons();
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

    std::string body;
    if (!title.empty() && !text.empty())
        body = title + ": " + text;
    else if (!title.empty())
        body = title;
    else
        body = text;

    Logger::info("[NOTIFICATION] Body: " + np.body.dump(1));

    std::string icon_hash = np.body.value("payloadHash", "");

    if (!np.payload.empty() && !icon_hash.empty())
        write_app_icon(icon_hash, np.payload);

    if (silent) return true;
    Logger::info("[NOTIFICATION] Posting: " + icon_hash + "-" + id);
    post_notification(icon_hash.empty() ? kAppId : std::string(kAppId) + "_" + icon_hash, app, body, id);
    return true;
}

void NotificationPlugin::post_notification(const std::string& app_id,
                                           const std::string& title,
                                           const std::string& body,
                                           const std::string& id) {
#ifdef __SWITCH__
    mkdir(kNotifyDir, 0755);

    static std::atomic<int> s_counter{0};
    std::string uid = sanitize_filename(id);
    if (uid.empty()) uid = std::to_string(s_counter.fetch_add(1));

    std::string path     = std::string(kNotifyDir) + "/" + app_id + "-" + uid + ".notify";

    nlohmann::json notify_json = {
        {"title",      title},
        {"text",       body},
        {"duration",   4000},
        {"show_time",  "true"},
        {"split_type", "word"},
        {"alignment",  "left"},
    };

    std::ofstream f(path);
    if (f.is_open()) {
        f << notify_json.dump(2);
        f.close();
    } else {
        Logger::error("[NOTIFICATION] Failed to write notify file: " + path);
    }
#endif
    (void)app_id; (void)title; (void)body; (void)id;
}

void NotificationPlugin::write_app_icon(const std::string& icon_hash, const std::vector<uint8_t>& png_data) {
#ifdef __SWITCH__
    int w, h, channels;
    uint8_t* img = stbi_load_from_memory(png_data.data(), static_cast<int>(png_data.size()),
                                         &w, &h, &channels, 4);
    if (!img) {
        Logger::warn("[NOTIFICATION] Failed to decode icon PNG for hash " + icon_hash);
        return;
    }

    mkdir("/config/ultrahand", 0755);
    mkdir("/config/ultrahand/assets", 0755);
    mkdir("/config/ultrahand/assets/notifications", 0755);

    std::string icon_path = "/config/ultrahand/assets/notifications/kdeconnect_" + icon_hash + ".rgba";

    static constexpr int OUT = 50;
    uint8_t px[OUT * OUT * 4];
    for (int oy = 0; oy < OUT; oy++) {
        int sy = oy * h / OUT;
        for (int ox = 0; ox < OUT; ox++) {
            int sx = ox * w / OUT;
            int si = (sy * w + sx) * 4;
            int di = (oy * OUT + ox) * 4;
            px[di]   = img[si];
            px[di+1] = img[si+1];
            px[di+2] = img[si+2];
            px[di+3] = img[si+3];
        }
    }
    stbi_image_free(img);

    FILE* f = fopen(icon_path.c_str(), "wb");
    if (f) {
        fwrite(px, 1, sizeof(px), f);
        fclose(f);
    } else {
        Logger::error("[NOTIFICATION] Failed to write icon: " + icon_path);
    }
#endif
    (void)icon_hash; (void)png_data;
}

void NotificationPlugin::request_active_notifications() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::NotificationRequest;
    pkt.body = { {"request", true} };
    send_packet(pkt);
}
