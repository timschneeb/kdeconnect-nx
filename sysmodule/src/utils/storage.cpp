#include "storage.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "../net/network_packet.h"
#include "../plugins/plugin_registry.h"

namespace {
std::string hostname_or_default() {
#if __SWITCH__
    setInitialize();
    SetSysDeviceNickName name;
    setGetDeviceNickname(&name);
    setExit();
    return name.nickname;
#else
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
#ifdef __SWITCH__
    base_path_ =  std::filesystem::path("/config/kdeconnect");
#else
    const char* home = getenv("HOME");
    const std::filesystem::path home_path = home ? std::filesystem::path(home) : std::filesystem::current_path();
    base_path_ = home_path / ".config" / "minikdeconnect";
#endif

    std::filesystem::create_directories(base_path_ / "paired");
}

DeviceInfo Storage::load_or_create_local_device(DeviceProvider* device_provider) const {
    DeviceInfo info;
    info.name = hostname_or_default();
    info.type = "tablet";
    info.protocol_version = kProtocolVersion;

    info.incoming_capabilities = PluginRegistry::get_all_supported_packet_types(device_provider);
    info.incoming_capabilities.push_back(PacketTypes::Pair); // Core capability

    info.outgoing_capabilities = PluginRegistry::get_all_outgoing_packet_types(device_provider);
    info.outgoing_capabilities.push_back(PacketTypes::Pair); // Core capability

    return info;
}

std::optional<PairedDeviceInfo> Storage::load_paired_device(const std::string& device_id) const {
    auto path = paired_path(base_path_, device_id);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    const std::string content = read_file(path);
    if (content.empty()) return std::nullopt;
    const auto data = nlohmann::json::parse(content, nullptr, false);
    if (data.is_discarded()) return std::nullopt;

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
    write_file(paired_path(base_path_, info.id), data.dump(2));
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

std::string Storage::read_file(const std::string &path) {
#if defined(__SWITCH__)
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) return {};

    FsFile file;
    if (R_FAILED(fsFsOpenFile(fs, path.c_str(), FsOpenMode_Read, &file))) {
        return {};
    }

    s64 size = 0;
    if (R_FAILED(fsFileGetSize(&file, &size)) || size <= 0) {
        fsFileClose(&file);
        return {};
    }

    std::string out(static_cast<size_t>(size), '\0');
    u64 read = 0;
    Result rc = fsFileRead(&file, 0, out.data(), static_cast<size_t>(size), FsReadOption_None, &read);
    fsFileClose(&file);
    if (R_FAILED(rc) || read == 0) return {};
    if (read < static_cast<u64>(size)) out.resize(static_cast<size_t>(read));
    return out;
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return {}; }
    std::string out(static_cast<size_t>(size), '\0');
    fread(out.data(), 1, static_cast<size_t>(size), f);
    fclose(f);
    return out;
#endif
}

bool Storage::write_file(const std::string &path, const std::string &data) {
#if defined(__SWITCH__)
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) return false;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    fsFsCreateFile(fs, path.c_str(), 0, 0);

    FsFile file;
    if (R_FAILED(fsFsOpenFile(fs, path.c_str(), FsOpenMode_Write, &file))) {
        return false;
    }

    Result rc = fsFileWrite(&file, 0, data.data(), data.size(), FsWriteOption_Flush);
    fsFileClose(&file);
    return R_SUCCEEDED(rc);
#else
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t written = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return written == data.size();
#endif
}
