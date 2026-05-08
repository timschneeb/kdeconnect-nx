#include "storage.h"

#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#if __has_include(<unistd.h>)
#include <unistd.h>
#endif

namespace {


std::string hostname_or_default() {
#if __has_include(<unistd.h>)
    char buffer[256] = {};
    if (gethostname(buffer, sizeof(buffer) - 1) == 0) {
        return std::string(buffer) + "-mini";
    }
#endif
    return "MiniKDEConnect";
}

std::filesystem::path paired_path(const std::filesystem::path& base, const std::string& device_id) {
    return base / "paired" / (device_id + ".json");
}
} // namespace

Storage::Storage() {
    const char* home = getenv("HOME");
    const std::filesystem::path home_path = home ? std::filesystem::path(home) : std::filesystem::current_path();
    base_path_ = home_path / ".config" / "minikdeconnect";
    std::filesystem::create_directories(base_path_ / "paired");
}

DeviceInfo Storage::load_or_create_local_device() const {
    DeviceInfo info;
    info.name = hostname_or_default();
    info.type = "tablet";
    info.protocol_version = kProtocolVersion;
    info.incoming_capabilities = {"kdeconnect.ping", "kdeconnect.pair"};
    info.outgoing_capabilities = {"kdeconnect.ping", "kdeconnect.pair"};
    return info;
}

std::optional<PairedDeviceInfo> Storage::load_paired_device(const std::string& device_id) const {
    auto path = paired_path(base_path_, device_id);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::ifstream in(path);
    nlohmann::json data;
    in >> data;
    PairedDeviceInfo info;
    info.info.id = data.value("deviceId", device_id);
    info.info.name = data.value("deviceName", std::string("unknown"));
    info.info.type = data.value("deviceType", std::string("desktop"));
    info.info.protocol_version = data.value("protocolVersion", kProtocolVersion);
    info.certificate_pem = data.value("certificatePem", std::string());
    return info;
}

void Storage::save_paired_device(const DeviceInfo& info, const std::string& certificate_pem) const {
    nlohmann::json data;
    data["deviceId"] = info.id;
    data["deviceName"] = info.name;
    data["deviceType"] = info.type;
    data["protocolVersion"] = info.protocol_version;
    data["certificatePem"] = certificate_pem;
    std::ofstream out(paired_path(base_path_, info.id));
    out << data.dump(2);
}

auto Storage::remove_paired_device(const std::string &device_id) const -> void {
    const auto path = paired_path(base_path_, device_id);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

std::filesystem::path Storage::base_dir() const {
    return base_path_;
}

std::filesystem::path Storage::cert_path() const {
    return base_path_ / "cert.pem";
}

std::filesystem::path Storage::key_path() const {
    return base_path_ / "key.pem";
}

