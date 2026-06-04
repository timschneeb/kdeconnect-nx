#include "notification_plugin.h"
#include "utils/logger.h"
#include "utils/storage.h"
#include "utils/settings_store.h"

#include <atomic>
#include <cstdio>
#include <string>

#ifdef __SWITCH__
#include <malloc.h>
#include <cstring>
#include <png.h>
#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>

#include "psc_monitor.h"
#endif

static constexpr auto kNotifyDir = "/config/ultrahand/notifications";
static constexpr auto kNotifyFlag = "/config/ultrahand/flags/NOTIFICATIONS.flag";
static constexpr auto kAppId     = "kdeconnect";

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
    static constexpr auto kIconDir = "/config/ultrahand/assets/notifications";
    DIR* d = opendir(kIconDir);
    if (!d) return;
    dirent* entry;
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

void NotificationPlugin::on_connected(const bool paired) {
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

    std::string icon_hash = np.body.value("payloadHash", "");
    if (icon_hash.empty()) {
        auto it = m_app_icon_hash.find(app);
        if (it != m_app_icon_hash.end())
            icon_hash = it->second;
    } else {
        if (m_app_icon_hash.size() >= kMaxAppIconHashes && !m_app_icon_hash.contains(app))
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
                for (auto jt = std::next(oldest); jt != m_posted_ids.end(); ++jt)
                    if (jt->second.when < oldest->second.when) oldest = jt;
                m_posted_ids.erase(oldest);
            }
            m_posted_ids[id] = {!icon_hash.empty(), time, now};
        }
    }

    if (silent) return true;
    if (!SettingsStore::get(KdecBoolSettingKey::NotificationShowRemoteMessages)) return true;
    if (!Storage::file_exists(kNotifyFlag)) return true;
#ifdef __SWITCH__
    if (!PscMonitor::is_awake()) return true;
#endif
    post_app_notification(id, app, title, text, icon_hash);
    return true;
}

void NotificationPlugin::request_active_notifications() const {
    NetworkPacket pkt;
    pkt.type = PacketTypes::NotificationRequest;
    pkt.body.set("request", true);
    send_packet(pkt);
}

void NotificationPlugin::post_app_notification(const std::string& id, const std::string& app, const std::string& title, const std::string& text, const std::string& icon_hash) {
    const bool show_icon = SettingsStore::get(KdecBoolSettingKey::NotificationShowIcon);
    const std::string app_id = (show_icon && !icon_hash.empty())
        ? std::string(kAppId) + "_" + icon_hash
        : kAppId;
    const int duration = SettingsStore::get(KdecIntSettingKey::NotificationDuration);

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
            if (!title.empty() && !text.empty())
                post_body = title + ": " + text;
            else if (!title.empty())
                post_body = title;
            else
                post_body = text;
            break;
    }

    post_notification(app_id, post_title, post_body, id, duration);
}

void NotificationPlugin::post_notification(const std::string& app_id,
                                           const std::string& title,
                                           const std::string& body,
                                           const std::string& id,
                                           const int duration) {
#ifdef __SWITCH__
    static std::atomic s_counter{0};
    std::string uid = sanitize_filename(id);
    if (uid.empty()) uid = std::to_string(s_counter.fetch_add(1));

    std::string path     = std::string(kNotifyDir) + "/" + app_id + "-" + uid + ".notify";

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
        font_size = SettingsStore::get(KdecIntSettingKey::NotificationFontSize);
    }

    JsonBody notify_json;
    notify_json.set("title",      title)
               .set("text",       body)
               .set("duration",   duration)
               .set("show_time",  SettingsStore::get(KdecBoolSettingKey::NotificationShowTime) ? "true" : "false")
               .set("split_type", "word")
               .set("alignment",  "left")
               .set("font_size",  font_size);

    if (!Storage::write_file(path, notify_json.dump(2))) {
        Logger::error("Failed to write notify file: %s", path.c_str());
    }
#endif
    (void)app_id; (void)title; (void)body; (void)id; (void)duration;
}

void NotificationPlugin::write_app_icon(const std::string& icon_hash, const NetworkPacket& np) const {
#ifdef __SWITCH__
    const std::string icon_path = "/config/ultrahand/assets/notifications/kdeconnect_" + icon_hash + ".rgba";
    const std::string icon_path_png = "/config/ultrahand/assets/notifications/.kdeconnect_" + icon_hash + ".png";

    // Icon content is addressed by its hash; if the file already exists it is identical.
    if (Storage::file_exists(icon_path)) return;

    auto np_with_payload = np;
    if (!provider_->download_payload(provider_->device(device_id_), np_with_payload, icon_path_png)) {
        Logger::warn("Failed to download icon payload for hash %s", icon_hash.c_str());
        return;
    }

    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) {
        Logger::error("write_app_icon: fsdevGetDeviceFileSystem failed");
        remove(icon_path_png.c_str());
        return;
    }

    static constexpr int  kMaxIconPixels = 256 * 256;
    static constexpr int  kMaxIconDim    = 256;
    static constexpr int  OUT            = 50;
    static constexpr s64  kRgbaSize      = OUT * OUT * 4;

    // All stack buffers declared before setjmp: no C++ objects straddle longjmp.
    uint8_t row_buf[kMaxIconDim * 4];
    uint8_t out_row[OUT * 4];
    char    path_in[icon_path_png.size()];
    char    path_out[icon_path.size()];

    memcpy(path_in,  icon_path_png.c_str(), icon_path_png.size() + 1);
    memcpy(path_out, icon_path.c_str(),     icon_path.size() + 1);

    // Volatile flags so the longjmp cleanup sees whether each file is open.
    volatile bool file_in_open  = false;
    volatile bool file_out_open = false;
    FsFile file_in  = {};
    FsFile file_out = {};
    s64    out_offset = 0;

    // Context for the libpng read callback, pointer kept valid for its lifetime.
    struct PngReadCtx { FsFile* file; s64 offset; };
    PngReadCtx read_ctx{&file_in, 0};

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) { remove(icon_path_png.c_str()); return; }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        remove(icon_path_png.c_str());
        return;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        Logger::warn("Failed to decode icon for hash %s", icon_hash.c_str());
        if (file_in_open)  fsFileClose(&file_in);
        if (file_out_open) { fsFileClose(&file_out); fsFsDeleteFile(fs, path_out); }
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        remove(icon_path_png.c_str());
        return;
    }

    {
        const Result rc = fsFsOpenFile(fs, path_in, FsOpenMode_Read, &file_in);
        if (R_FAILED(rc)) {
            Logger::warn("Failed to open temp PNG for hash %s: %d-%d",
                         icon_hash.c_str(), R_MODULE(rc), R_DESCRIPTION(rc));
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
            remove(icon_path_png.c_str());
            return;
        }
    }
    file_in_open = true;

    // fsFileRead requires a stack destination buffer to avoid 0xD401 IPC errors.
    png_set_read_fn(png_ptr, &read_ctx,
        [](png_structp png, png_bytep buf, png_size_t n) {
            constexpr size_t kChunk = 0x400;
            uint8_t stack_buf[kChunk];
            auto* ctx = static_cast<PngReadCtx*>(png_get_io_ptr(png));
            size_t remaining = n;
            while (remaining > 0) {
                const size_t to_read = remaining < kChunk ? remaining : kChunk;
                u64 bytes_read = 0;
                const Result rc = fsFileRead(ctx->file, ctx->offset,
                                             stack_buf, to_read,
                                             FsReadOption_None, &bytes_read);
                if (R_FAILED(rc) || bytes_read == 0) { png_error(png, "read error"); return; }
                memcpy(buf, stack_buf, bytes_read);
                buf           += bytes_read;
                ctx->offset   += static_cast<s64>(bytes_read);
                remaining     -= bytes_read;
            }
        });

    png_read_info(png_ptr, info_ptr);

    const int w = static_cast<int>(png_get_image_width(png_ptr, info_ptr));
    const int h = static_cast<int>(png_get_image_height(png_ptr, info_ptr));

    if (w > kMaxIconDim || h > kMaxIconDim || w * h > kMaxIconPixels) {
        Logger::warn("Icon %s too large (%dx%d), skipping", icon_hash.c_str(), w, h);
        fsFileClose(&file_in); file_in_open = false;
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        remove(icon_path_png.c_str());
        return;
    }

    // Normalize any PNG variant to 8-bit RGBA.
    const png_byte bit_depth  = png_get_bit_depth(png_ptr, info_ptr);
    const png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    // Pre-create the output file at exact size, then open for writing.
    fsFsDeleteFile(fs, path_out);
    {
        const Result rc = fsFsCreateFile(fs, path_out, kRgbaSize, 0);
        if (R_FAILED(rc)) {
            Logger::error("Failed to create RGBA file for hash %s: %d-%d",
                          icon_hash.c_str(), R_MODULE(rc), R_DESCRIPTION(rc));
            fsFileClose(&file_in); file_in_open = false;
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
            remove(icon_path_png.c_str());
            return;
        }
    }
    {
        const Result rc = fsFsOpenFile(fs, path_out, FsOpenMode_Write, &file_out);
        if (R_FAILED(rc)) {
            Logger::error("Failed to open RGBA file for hash %s: %d-%d",
                          icon_hash.c_str(), R_MODULE(rc), R_DESCRIPTION(rc));
            fsFsDeleteFile(fs, path_out);
            fsFileClose(&file_in); file_in_open = false;
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
            remove(icon_path_png.c_str());
            return;
        }
    }
    file_out_open = true;

    // Nearest-neighbour downsample to OUTxOUT; write one output row at a time
    // so the full decoded image never exists in memory simultaneously.
    // out_row is stack-allocated, safe to pass directly to fsFileWrite.
    int prev_sy = -1;
    for (int oy = 0; oy < OUT; oy++) {
        const int sy = oy * h / OUT;
        while (prev_sy < sy) {
            ++prev_sy;
            png_read_row(png_ptr, row_buf, nullptr);
        }
        for (int ox = 0; ox < OUT; ox++) {
            const int sx = ox * w / OUT;
            out_row[ox * 4]     = row_buf[sx * 4];
            out_row[ox * 4 + 1] = row_buf[sx * 4 + 1];
            out_row[ox * 4 + 2] = row_buf[sx * 4 + 2];
            out_row[ox * 4 + 3] = row_buf[sx * 4 + 3];
        }
        fsFileWrite(&file_out, out_offset, out_row, sizeof(out_row), FsWriteOption_None);
        out_offset += static_cast<s64>(sizeof(out_row));
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fsFileFlush(&file_out);
    fsFileClose(&file_in);
    fsFileClose(&file_out);
    remove(icon_path_png.c_str());
    malloc_trim(0);
#endif
    (void)icon_hash; (void)np;
}
