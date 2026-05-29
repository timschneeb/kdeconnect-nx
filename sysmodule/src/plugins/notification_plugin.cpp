#include "notification_plugin.h"
#include "utils/logger.h"
#include "utils/storage.h"
#include "../utils/settings_store.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#ifdef __SWITCH__
#include <malloc.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#include <dirent.h>
#include <sys/stat.h>
#endif

static constexpr const char* kNotifyDir = "/config/ultrahand/notifications";
static constexpr const char* kIconPath  = "/config/ultrahand/assets/notifications/kdeconnect.rgba";
static constexpr const char* kAppId     = "kdeconnect";

static constexpr size_t kMaxPostedIds     = 10;
static constexpr size_t kMaxAppIconHashes = 100;

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
    mkdir("/config/ultrahand", 0755);
    mkdir("/config/ultrahand/assets", 0755);
    mkdir("/config/ultrahand/assets/notifications", 0755);
#endif
}

void NotificationPlugin::on_connected(bool paired) {
    if (paired) request_active_notifications();
}

bool NotificationPlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::Notification) return false;

    if (np.body.value("isCancel", false)) {
        std::string cancel_id = np.body.value("id", "");
        Logger::info("Dismissed: %s", cancel_id.c_str());
        m_posted_ids.erase(cancel_id);
        return true;
    }

    bool silent      = np.body.value("silent", false);
    std::string app   = np.body.value("appName", "Unknown");
    std::string title = np.body.value("title", "");
    std::string text  = np.body.value("text", "");
    std::string id    = np.body.value("id", "");
    std::string time  = np.body.value("time", "");

    Logger::info("%s: %s", app.c_str(), title.c_str());

    std::string body;
    if (!title.empty() && !text.empty())
        body = title + ": " + text;
    else if (!title.empty())
        body = title;
    else
        body = text;

    std::string icon_hash = np.body.value("payloadHash", "");
    if (icon_hash.empty()) {
        auto it = m_app_icon_hash.find(app);
        if (it != m_app_icon_hash.end())
            icon_hash = it->second;
    } else {
        if (m_app_icon_hash.size() >= kMaxAppIconHashes && !m_app_icon_hash.count(app))
            m_app_icon_hash.erase(m_app_icon_hash.begin());
        m_app_icon_hash[app] = icon_hash;
    }

    if (np.has_payload() && !icon_hash.empty()) {
        write_app_icon(icon_hash, np);
    }

    if (!id.empty()) {
        auto now = std::chrono::steady_clock::now();
        auto it  = m_posted_ids.find(id);
        if (it != m_posted_ids.end() && it->second.time == time &&
                now - it->second.when < std::chrono::seconds(2)) {
            if (it->second.has_icon || icon_hash.empty())
                return true; // skip weaker or equal duplicate
            // Upgrade: old was posted with default icon, now we have the real one.
            // Remove the old notify file so Ultrahand will process the new one.
            std::string uid = sanitize_filename(id);
#ifdef __SWITCH__
            if (!uid.empty())
                remove((std::string(kNotifyDir) + "/" + kAppId + "-" + uid + ".notify").c_str());
#endif
            it->second.has_icon = true;
        } else {
            if (m_posted_ids.size() >= kMaxPostedIds) {
                auto oldest = m_posted_ids.begin();
                for (auto it = std::next(oldest); it != m_posted_ids.end(); ++it)
                    if (it->second.when < oldest->second.when) oldest = it;
                m_posted_ids.erase(oldest);
            }
            m_posted_ids[id] = {!icon_hash.empty(), time, now};
        }
    }

    if (silent) return true;
    if (!SettingsStore::get(KdecBoolSettingKey::NotificationShowRemoteMessages)) return true;

    const bool show_icon = SettingsStore::get(KdecBoolSettingKey::NotificationShowIcon);
    const std::string app_id = (show_icon && !icon_hash.empty())
        ? std::string(kAppId) + "_" + icon_hash
        : kAppId;
    const int duration = static_cast<int>(SettingsStore::get(KdecIntSettingKey::NotificationDuration));

    Logger::info("Posting: %s-%s", icon_hash.c_str(), id.c_str());

    const auto style = static_cast<NotificationStyle>(SettingsStore::get(KdecIntSettingKey::NotificationStyle));
    std::string post_title, post_body;
    switch (style) {
        case NotificationStyle::AppName_Title:
            post_title = app;
            post_body  = title;
            break;
        case NotificationStyle::Title_Body:
            post_title = title.empty() ? app : title;
            post_body  = text;
            break;
        case NotificationStyle::TitleAppName_Body:
            post_title = title.empty() ? app : title + " \xc2\xb7 " + app;
            post_body  = text;
            break;
        case NotificationStyle::AppNameTitle_Body:
            post_title = title.empty() ? app : app + " \xc2\xb7 " + title;
            post_body = text;
            break;
        default: // AppName_TitleBody
            post_title = app;
            post_body  = body;
            break;
    }

    post_notification(app_id, post_title, post_body, id, duration);
    return true;
}

void NotificationPlugin::post_notification(const std::string& app_id,
                                           const std::string& title,
                                           const std::string& body,
                                           const std::string& id,
                                           int duration) {
#ifdef __SWITCH__
    static std::atomic<int> s_counter{0};
    std::string uid = sanitize_filename(id);
    if (uid.empty()) uid = std::to_string(s_counter.fetch_add(1));

    std::string path     = std::string(kNotifyDir) + "/" + app_id + "-" + uid + ".notify";

    nlohmann::json notify_json = {
        {"title",      title},
        {"text",       body},
        {"duration",   duration},
        {"show_time",  SettingsStore::get(KdecBoolSettingKey::NotificationShowTime) ? "true" : "false"},
        {"split_type", "word"},
        {"alignment",  "left"},
    };

    {
        int font_size;
        if (SettingsStore::get(KdecBoolSettingKey::NotificationDynamicFontSize)) {
            static constexpr int kBase = 24;
            static constexpr int kMin  = 16;
            // ~30 chars/line at size 24 in a 406px area; target 3 lines before shrinking
            static constexpr int kTargetChars = 90;
            const int len = static_cast<int>(body.length());
            font_size = (len > kTargetChars)
                ? std::max(kMin, kBase * kTargetChars / len)
                : kBase;
        } else {
            font_size = static_cast<int>(SettingsStore::get(KdecIntSettingKey::NotificationFontSize));
        }
        notify_json["font_size"] = font_size;
    }

    if (!Storage::write_file(path, notify_json.dump(2))) {
        Logger::error("Failed to write notify file: %s", path.c_str());
    }
#endif
    (void)app_id; (void)title; (void)body; (void)id;
}

void NotificationPlugin::write_app_icon(const std::string& icon_hash, const NetworkPacket& np) const {
#ifdef __SWITCH__
    const std::string icon_path = "/config/ultrahand/assets/notifications/kdeconnect_" + icon_hash + ".rgba";

    // Icon content is addressed by its hash; if the file already exists it is identical.
    if (Storage::file_exists(icon_path)) return;

    auto np_with_payload = np;
    if (!provider_->download_payload(provider_->device(device_id_), np_with_payload) ||
            np_with_payload.payload.empty()) {
        Logger::warn("Failed to download icon payload for hash %s", icon_hash.c_str());
        return;
    }

    int w, h, channels;
    if (!stbi_info_from_memory(np_with_payload.payload.data(),
                               static_cast<int>(np_with_payload.payload.size()),
                               &w, &h, &channels)) {
        Logger::warn("Failed to read icon header for hash %s", icon_hash.c_str());
        return;
    }
    // A decoded RGBA buffer of w*h*4 bytes is a large transient heap allocation;
    // The Android client is capped at 128x128.
    static constexpr int kMaxIconPixels = 256 * 256;
    if (w * h > kMaxIconPixels) {
        Logger::warn("Icon %s too large (%dx%d), skipping decode", icon_hash.c_str(), w, h);
        return;
    }
    uint8_t* img = stbi_load_from_memory(np_with_payload.payload.data(),
                                         static_cast<int>(np_with_payload.payload.size()),
                                         &w, &h, &channels, 4);
    if (!img) {
        Logger::warn("Failed to decode icon for hash %s (%zu B, %dx%d)",
                     icon_hash.c_str(), np_with_payload.payload.size(), w, h);
        return;
    }

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
    malloc_trim(0);

    std::string payload(reinterpret_cast<const char*>(px), sizeof(px));
    if (!Storage::write_file(icon_path, payload)) {
        Logger::error("Failed to write icon: %s", icon_path.c_str());
    }
#endif
    (void)icon_hash; (void)np;
}

void NotificationPlugin::request_active_notifications() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::NotificationRequest;
    pkt.body = { {"request", true} };
    send_packet(pkt);
}
