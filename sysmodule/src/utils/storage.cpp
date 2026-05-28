#include "storage.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "../net/network_packet.h"
#include "../plugins/plugin_registry.h"

// The sdmc FsFileSystem session is not thread-safe. All FS operations hold this lock.
static std::mutex s_fs_mutex;

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
    {
        std::lock_guard lock(s_fs_mutex);
        if (!std::filesystem::exists(path)) return std::nullopt;
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

bool Storage::file_exists(const std::string& path) {
#if defined(__SWITCH__)
    std::lock_guard lock(s_fs_mutex);
#endif
    return std::filesystem::exists(path);
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
    std::lock_guard lock(s_fs_mutex);
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
    std::lock_guard lock(s_fs_mutex);
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) return {};

    // Path and data buffers must be in stack memory for FS IPC (0xD401 otherwise).
    char path_buf[FS_MAX_PATH];
    snprintf(path_buf, sizeof(path_buf), "%s", path.c_str());

    FsFile file;
    Result rc;

    if (rc = fsFsOpenFile(fs, path_buf, FsOpenMode_Read, &file); R_FAILED(rc)) {
        Logger::error("Failed to open file %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
        return {};
    }

    s64 size = 0;
    if (rc = fsFileGetSize(&file, &size); R_FAILED(rc) || size <= 0) {
        Logger::error("Failed to get file size of %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
        fsFileClose(&file);
        return {};
    }

    // Read through a stack buffer: heap buffers may trigger 0xD401.
    static constexpr size_t kChunk = 0x1000;
    char chunk_buf[kChunk];
    std::string out;
    out.reserve(static_cast<size_t>(size));
    s64 offset = 0;
    s64 remaining = size;
    while (remaining > 0) {
        const size_t to_read = static_cast<size_t>(std::min(remaining, static_cast<s64>(kChunk)));
        u64 bytes_read = 0;
        rc = fsFileRead(&file, offset, chunk_buf, to_read, FsReadOption_None, &bytes_read);
        if (R_FAILED(rc) || bytes_read == 0) {
            Logger::error("Failed to read file %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
            fsFileClose(&file);
            return {};
        }
        out.append(chunk_buf, bytes_read);
        offset += static_cast<s64>(bytes_read);
        remaining -= static_cast<s64>(bytes_read);
    }
    fsFileClose(&file);
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
    std::lock_guard lock(s_fs_mutex);
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs) return false;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path().string());

    // Path and data buffers must be in stack memory for FS IPC (0xD401 otherwise).
    char path_buf[FS_MAX_PATH];
    snprintf(path_buf, sizeof(path_buf), "%s", path.c_str());

    // Create the file if it doesn't exist; ignore the error if it does.
    fsFsCreateFile(fs, path_buf, static_cast<s64>(data.size()), 0);

    FsFile file;
    Result rc;

    if (rc = fsFsOpenFile(fs, path_buf, FsOpenMode_Write | FsOpenMode_Append, &file); R_FAILED(rc)) {
        Logger::error("Failed to open file %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
        return false;
    }

    // Resize to match actual data: required when overwriting a smaller file.
    if (rc = fsFileSetSize(&file, static_cast<s64>(data.size())); R_FAILED(rc)) {
        Logger::error("Failed to set file size of %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
        fsFileClose(&file);
        return false;
    }

    // Write through a stack buffer: heap buffers may trigger 0xD401.
    static constexpr size_t kChunk = 0x1000;
    char chunk_buf[kChunk];
    s64 offset = 0;
    const char* src = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        const size_t to_write = std::min(remaining, kChunk);
        memcpy(chunk_buf, src, to_write);
        rc = fsFileWrite(&file, offset, chunk_buf, to_write, FsWriteOption_None);
        if (R_FAILED(rc)) {
            Logger::error("Failed to write file %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
            fsFileClose(&file);
            return false;
        }
        offset += static_cast<s64>(to_write);
        src += to_write;
        remaining -= to_write;
    }
    fsFileFlush(&file);
    fsFileClose(&file);
    return true;
#else
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t written = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return written == data.size();
#endif
}
