#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "../net/kdeconnect_types.h"
#include "../plugins/plugin.h"

struct PairedDeviceInfo {
    DeviceInfo info;
    std::string certificate_pem;
};

class Storage {
public:
    Storage();

    DeviceInfo load_or_create_local_device(DeviceProvider *device_provider) const;

    [[nodiscard]] std::optional<PairedDeviceInfo> load_paired_device(const std::string& device_id) const;
    void save_paired_device(const DeviceInfo& info, const std::string& certificate_pem) const;
    void remove_paired_device(const std::string& device_id) const;

    [[nodiscard]] std::filesystem::path base_dir() const;
    [[nodiscard]] std::filesystem::path cert_path() const;
    [[nodiscard]] std::filesystem::path key_path() const;

    static std::string read_file(const std::string& path);
    static bool write_file(const std::string& path, const std::string& data);

private:
    std::filesystem::path base_path_;
};

