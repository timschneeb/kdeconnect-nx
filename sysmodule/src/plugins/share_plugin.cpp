#include "share_plugin.h"
#include "utils/logger.h"

#include <chrono>
#include <cstdio>

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

bool SharePlugin::send_screenshot() const {
    if (!provider_) return false;

    auto buffer = capture_screenshot_to_buffer();
    if (buffer.empty()) return false;

    auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    char filename[40];
    snprintf(filename, sizeof(filename), "screenshot_%lld.jpg", static_cast<long long>(ts));

    NetworkPacket pkt;
    pkt.type = PacketTypes::ShareRequest;
    pkt.body.set("filename", std::string(filename));
    pkt.send_payload = std::move(buffer);

    return provider_->send_payload(device_id_, std::move(pkt));
}

std::vector<unsigned char> SharePlugin::capture_screenshot_to_buffer() {
    std::vector<unsigned char> jpegBuffer;

#ifdef __SWITCH__
    std::lock_guard lock(s_screenshot_mutex_);
    if (R_FAILED(capsscInitialize())) {
        Logger::error("Failed to initialize caps:sc");
        return jpegBuffer;
    }

    jpegBuffer.resize(CAPSSC_JPEG_BUFFER_SIZE);
    u64 outSize = 0;

    Result rc = capsscCaptureJpegScreenShot(&outSize, jpegBuffer.data(),
                                            CAPSSC_JPEG_BUFFER_SIZE,
                                            ViLayerStack_Screenshot, 100000000);
    capsscExit();

    if (R_FAILED(rc)) {
        Logger::error("Failed to capture screenshot: 0x%X", rc);
        return {};
    }

    // Release the unused tail of the 512 KB capture buffer before queuing for send.
    jpegBuffer.resize(outSize);
    jpegBuffer.shrink_to_fit();
#endif
    return jpegBuffer;
}