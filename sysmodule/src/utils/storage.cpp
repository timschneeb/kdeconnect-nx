#include "storage.h"

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "../net/network_packet.h"
#include "../plugins/plugin_registry.h"

// All FS operations hold this lock.
static std::mutex s_fs_mutex;

namespace {

std::string path_stem(const std::string& path) {
    auto slash = path.rfind('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

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
    return "MiniKDEConnect";
#endif
}

std::string paired_path(const std::string& base, const std::string& device_id) {
    return base + "/paired/" +  device_id + ".json";
}

} // namespace

void Storage::make_directories(const std::string& path) {
#ifdef __SWITCH__
    std::string tmp = "sdmc:/" + path;
#else
    std::string tmp = "/" + path;
#endif
    for (size_t i = 1; i < tmp.size(); ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp.c_str(), 0755);
            tmp[i] = '/';
        }
    }
    mkdir(tmp.c_str(), 0755);
}

Storage::Storage() {
#ifdef __SWITCH__
    base_path_ = "/config/kdeconnect";
#else
    const char* home = getenv("HOME");
    if (home) {
        base_path_ = std::string(home) + "/.config/minikdeconnect";
    } else {
        char cwd[4096] = {};
        base_path_ = getcwd(cwd, sizeof(cwd)) ? (std::string(cwd) + "/.config/minikdeconnect")
                                               : ".config/minikdeconnect";
    }
#endif
    make_directories(base_path_ + "/paired");
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
    const auto path = paired_path(base_path_, device_id);
    {
        std::lock_guard lock(s_fs_mutex);
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return std::nullopt;
    }
    const std::string content = read_file(path);
    if (content.empty()) return std::nullopt;
    auto data = JsonBody::parse(content.c_str());
    if (!data.has("deviceId") || !data.has("certificatePem")) return std::nullopt;

    PairedDeviceInfo info;
    info.info.id = data.value("deviceId", device_id);
    info.info.name = data.value("deviceName", "unknown");
    info.info.type = data.value("deviceType", "desktop");
    info.info.protocol_version = data.value("protocolVersion", kProtocolVersion);
    info.certificate_pem = data.value("certificatePem", "");
    return info;
}

std::vector<std::string> Storage::list_paired_device_ids() const {
    std::vector<std::string> ids;
    const std::string paired_dir = base_path_ + "/paired";
#ifdef __SWITCH__
    std::lock_guard lock(s_fs_mutex);
#endif
    DIR* dir = opendir(paired_dir.c_str());
    if (!dir) return ids;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name(entry->d_name);
        if (name.ends_with(".json"))
            ids.push_back(path_stem(name));
    }
    closedir(dir);
    return ids;
}

bool Storage::file_exists(const std::string& path) {
    struct stat st;
#if defined(__SWITCH__)
    std::lock_guard lock(s_fs_mutex);
#endif
    return stat(path.c_str(), &st) == 0;
}

void Storage::save_paired_device(const DeviceInfo& info, const std::string& certificate_pem) const {
    JsonBody data;
    data.set("deviceId",       info.id)
        .set("deviceName",     info.name)
        .set("deviceType",     info.type)
        .set("protocolVersion", info.protocol_version)
        .set("certificatePem", certificate_pem);
    write_file(paired_path(base_path_, info.id), data.dump(2));
}

auto Storage::remove_paired_device(const std::string &device_id) const -> void {
    const auto path = paired_path(base_path_, device_id);
    std::lock_guard lock(s_fs_mutex);
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        ::remove(path.c_str());
    }
}

std::string Storage::base_dir() const {
    return base_path_;
}

std::string Storage::cert_path() const {
    return base_path_ + "/cert.pem";
}

std::string Storage::key_path() const {
    return base_path_ + "/key.pem";
}

std::string Storage::read_file(const std::string &path) {
#if defined(__SWITCH__)
    std::lock_guard lock(s_fs_mutex);
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    if (!fs || path.size() >= FS_MAX_PATH) return {};

    // Path and data buffers must be in stack memory for FS IPC (0xD401 otherwise).
    char path_buf[FS_MAX_PATH];
    memcpy(path_buf, path.c_str(), path.size() + 1);

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

    // Create parent directories before opening the file.
    const auto slash = path.rfind('/');
    if (slash != std::string::npos)
        make_directories(path.substr(0, slash));

    if (path.size() >= FS_MAX_PATH) {
        Logger::error("Path too long: %s", path.c_str());
        return false;
    }

    FsFile file;
    Result rc;
    // Scope stack allocation
    {
        // Path and data buffers must be in stack memory for FS IPC (0xD401 otherwise).
        char path_buf[FS_MAX_PATH];
        strncpy(path_buf, path.c_str(), FS_MAX_PATH);
        // Create the file if it doesn't exist; ignore the error if it does.
        fsFsCreateFile(fs, path_buf, static_cast<s64>(data.size()), 0);

        if (rc = fsFsOpenFile(fs, path_buf, FsOpenMode_Write | FsOpenMode_Append, &file); R_FAILED(rc)) {
            Logger::error("Failed to open file %s: %d-%d", path_buf, R_MODULE(rc), R_DESCRIPTION(rc));
            return false;
        }
    }

    // Resize to match actual data: required when overwriting a smaller file.
    if (rc = fsFileSetSize(&file, static_cast<s64>(data.size())); R_FAILED(rc)) {
        Logger::error("Failed to set file size of %s: %d-%d", path.c_str(), R_MODULE(rc), R_DESCRIPTION(rc));
        fsFileClose(&file);
        return false;
    }

    // Write through a stack buffer: heap buffers may trigger 0xD401.
    static constexpr size_t kChunk = 0x400;
    char chunk_buf[kChunk];
    s64 offset = 0;
    const char* src = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        const size_t to_write = std::min(remaining, kChunk);
        memcpy(chunk_buf, src, to_write);
        rc = fsFileWrite(&file, offset, chunk_buf, to_write, FsWriteOption_None);
        if (R_FAILED(rc)) {
            Logger::error("Failed to write file %s: %d-%d", path.c_str(), R_MODULE(rc), R_DESCRIPTION(rc));
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
