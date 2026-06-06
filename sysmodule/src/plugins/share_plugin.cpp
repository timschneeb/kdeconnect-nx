#include "share_plugin.h"
#include "utils/logger.h"
#include "hiddbg_util.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "notification_plugin.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

std::mutex SharePlugin::s_url_mutex_;
std::mutex SharePlugin::s_screenshot_mutex_;
std::queue<std::string> SharePlugin::s_pending_urls_;

std::string SharePlugin::name() const { return "Share Plugin"; }
std::string SharePlugin::description() const { return "Receives shared URLs, text and files."; }

std::vector<std::string> SharePlugin::supported_packet_types() const {
    return { PacketTypes::ShareRequest };
}

std::vector<std::string> SharePlugin::outgoing_packet_types() const {
    return { PacketTypes::ShareRequest };
}

bool SharePlugin::on_packet_received(const NetworkPacket& np) {
    if (np.type != PacketTypes::ShareRequest) return false;

    auto get_device_name = [&]() -> std::string {
        if (const auto s = provider_->device(device_id_)) return s->info.name;
        return device_id_;
    };

    if (np.body.is_str("url")) {
        const std::string url = np.body.get_str("url");
        Logger::info("URL: %s", url.c_str());
        std::lock_guard lock(s_url_mutex_);
        if (s_pending_urls_.size() > 1) s_pending_urls_.pop();
        s_pending_urls_.push(url);
        return true;
    }

    if (np.body.is_str("text")) {
        const std::string text = np.body.get_str("text");
        Logger::info("Text: %s", text.c_str());
        NotificationPlugin::post_notification(
            "kdeconnect_share",
            "From " + get_device_name(),
            text,
            std::to_string(notification_id_.fetch_add(1)));
        return true;
    }

    if (np.body.is_str("filename") && np.has_payload()) {
        const std::string filename = np.body.get_str("filename");
        Logger::info("File: %s", filename.c_str());

        const std::string device_name = get_device_name();

        if (np.payload_size > 1024 * 1024) {
            NotificationPlugin::post_notification(
                "kdeconnect_share",
                "From " + device_name,
                "Receiving file... (" + std::to_string(np.payload_size/1024) + " KB)",
                std::to_string(notification_id_.fetch_add(1)));
        }

        // Prevent path traversal (save to SD root)
        std::string base = filename;
        auto sep = base.find_last_of("/\\");
        if (sep != std::string::npos) base = base.substr(sep + 1);
        if (base.empty()) base = "received_file";
        const std::string path = "/" + base;

        std::string notify_body;
        auto np_with_payload = np;
        if (provider_->download_payload(provider_->device(device_id_), np_with_payload, path)) {
            notify_body = "Saved: " + filename;
        } else {
            notify_body = "Failed to save: " + filename;
        }

        NotificationPlugin::post_notification(
            "kdeconnect_share",
            "From " + device_name,
            notify_body,
            std::to_string(notification_id_.fetch_add(1)));
        return true;
    }
    return false;
}

void SharePlugin::process_events() {
    open_pending_url();
}

bool SharePlugin::open_pending_url() {
    return false; // cannot launch browser from sysmodule

    std::string url;
    {
        std::lock_guard lock(s_url_mutex_);
        if (s_pending_urls_.empty()) return false;
        url = std::move(s_pending_urls_.front());
        s_pending_urls_.pop();
    }

#ifdef __SWITCH__
    WebCommonConfig config{};
    webPageCreate(&config, url.c_str());
    webConfigSetJsExtension(&config, true);
    webConfigSetPageCache(&config, true);
    webConfigSetBootLoadingIcon(&config, true);
    webConfigSetWhitelist(&config, ".*");
    webConfigSetScreenShot(&config, true);
    webConfigSetPointer(&config, true);
    webConfigSetMediaAutoPlay(&config, true);
    webConfigSetDisplayUrlKind(&config, true);
    webConfigSetMediaPlayerAutoClose(&config, false);
    webConfigSetPageScrollIndicator(&config, true);
    webConfigSetFooterFixedKind(&config, WebFooterFixedKind_Default);
    webConfigSetTransferMemory(&config, true);
    webConfigSetTouchEnabledOnContents(&config, true);
    webConfigSetWebAudio(&config, true);

    WebCommonReply ret{};
    webConfigShow(&config, &ret);
#else
    Logger::info("Would open URL: %s", url.c_str());
#endif
    return true;
}

#ifdef __SWITCH__
static void press_capture_button() {
    hiddbg_retain();

    HiddbgCaptureButtonAutoPilotState state{};
    state.buttons = BIT(0);
    hiddbgSetCaptureButtonAutoPilotState(&state);
    svcSleepThread(50'000'000LL); // hold 50 ms
    hiddbgUnsetCaptureButtonAutoPilotState();

    hiddbg_release();
}

static FsFileSystem s_album_fs{};
static bool s_album_fs_open = false;

static bool ensure_album_fs() {
    if (s_album_fs_open) return true;
    Result rc = fsOpenImageDirectoryFileSystem(&s_album_fs, FsImageDirectoryId_Sd);
    if (R_FAILED(rc)) {
        Logger::error("fsOpenImageDirectoryFileSystem failed: 0x%x", rc);
        return false;
    }
    s_album_fs_open = true;
    return true;
}

// Return the lexicographic-max .jpg filename in day_path, or empty string if none.
// day_path must be an absolute path within the album FsFileSystem, e.g. "/2024/06/04".
static void scan_newest_jpg(const char* day_path, char* out_name, const size_t out_size) {
    out_name[0] = '\0';

    FsDir dir;
    Result rc = fsFsOpenDirectory(&s_album_fs, day_path, FsDirOpenMode_ReadFiles, &dir);
    if (R_FAILED(rc)) {
        Logger::error("fsFsOpenDirectory failed for %s: 0x%x", day_path, rc);
        return;
    }

    FsDirectoryEntry entries[4];
    while (true) {
        s64 count = 0;
        rc = fsDirRead(&dir, &count, 4, entries);
        if (R_FAILED(rc) || count == 0) break;

        for (s64 i = 0; i < count; ++i) {
            const char* name = entries[i].name;
            size_t len = strlen(name);
            if (len < 21 || memcmp(name + len - 4, ".jpg", 4) != 0) continue;
            if (strcmp(name, out_name) > 0)
                strncpy(out_name, name, out_size - 1);
        }
    }
    fsDirClose(&dir);
}

// Poll the album day directory until a .jpg with a greater filename than
// prev_newest appears. Returns the new filename on success, empty on timeout.
static std::string poll_for_new_screenshot(const char* day_path, const char* prev_newest) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        svcSleepThread(100'000'000LL); // 100 ms

        char newest_name[FS_MAX_PATH] = {};
        scan_newest_jpg(day_path, newest_name, sizeof(newest_name));

        if (newest_name[0] != '\0' && strcmp(newest_name, prev_newest) > 0)
            return newest_name;
    }
    return {};
}

// Poll until the file size stops changing between two consecutive 100 ms checks,
// indicating the OS has finished writing the JPEG. Returns the stable size or -1 on timeout.
static s64 wait_for_file_write(const char* fs_path) {
    s64 prev = -1;
    for (int i = 0; i < 20; ++i) {
        svcSleepThread(100'000'000LL); // 100 ms
        FsFile f;
        if (R_FAILED(fsFsOpenFile(&s_album_fs, fs_path, FsOpenMode_Read, &f)))
            continue;
        s64 sz = 0;
        Result rc = fsFileGetSize(&f, &sz);
        fsFileClose(&f);
        if (R_FAILED(rc) || sz <= 0) continue;
        if (sz == prev)
            return sz;
        prev = sz;
    }
    return -1;
}

// RAII wrapper around FsFile for use as a shared streaming state.
struct FsReader {
    FsFile  file{};
    int64_t offset = 0;
    ~FsReader() { fsFileClose(&file); }
};

#endif // __SWITCH__

bool SharePlugin::send_screenshot() const {
    if (!provider_) return false;

#ifdef __SWITCH__
    std::string fs_path;
    int64_t file_size = 0;
    std::shared_ptr<FsReader> reader_state;

    {
        std::lock_guard lock(s_screenshot_mutex_);

        if (!ensure_album_fs()) {
            Logger::error("send_screenshot: cannot access album filesystem");
            return false;
        }

        // Build today's directory path within the album filesystem
        time_t now = time(nullptr);
        tm* lt = localtime(&now);
        char day_path[32];
        snprintf(day_path, sizeof(day_path), "/%04d/%02d/%02d",
                 1900 + lt->tm_year, 1 + lt->tm_mon, lt->tm_mday);

        char prev_newest[FS_MAX_PATH] = {};
        scan_newest_jpg(day_path, prev_newest, sizeof(prev_newest));

        press_capture_button();

        const std::string new_name = poll_for_new_screenshot(day_path, prev_newest);
        if (new_name.empty()) {
            Logger::error("send_screenshot: no new screenshot found in album after capture");
            return false;
        }

        char buf[FS_MAX_PATH];
        snprintf(buf, sizeof(buf), "%s/%s", day_path, new_name.c_str());
        fs_path = buf;

        // Wait for the OS to finish writing the JPEG before we open it for streaming
        file_size = wait_for_file_write(fs_path.c_str());
        if (file_size <= 0) {
            Logger::error("send_screenshot: file never stabilised: %s", fs_path.c_str());
            return false;
        }

        // Open the file once writing is complete
        reader_state = std::make_shared<FsReader>();
        Result rc = fsFsOpenFile(&s_album_fs, fs_path.c_str(), FsOpenMode_Read, &reader_state->file);
        if (R_FAILED(rc)) {
            Logger::error("send_screenshot: fsFsOpenFile failed for %s: 0x%x", fs_path.c_str(), rc);
            return false;
        }
    }

    char filename[64];
    snprintf(filename, sizeof(filename), "screenshot_%lld.jpg",
             static_cast<long long>(time(nullptr)));

    NetworkPacket pkt;
    pkt.type = PacketTypes::ShareRequest;
    pkt.body.set("filename", std::string(filename));

    auto state = std::move(reader_state);
    return provider_->send_payload_reader(device_id_, std::move(pkt), file_size,
        [state](void* buf, const size_t sz) -> size_t {
            u64 bytes_read = 0;
            Result rc = fsFileRead(&state->file, state->offset, buf, sz, 0, &bytes_read);
            if (R_FAILED(rc)) {
                Logger::error("send_screenshot: fsFileRead failed: 0x%x", rc);
                return 0;
            }
            state->offset += static_cast<int64_t>(bytes_read);
            return bytes_read;
        });
#else
    return false;
#endif
}