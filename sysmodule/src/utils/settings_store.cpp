#include "settings_store.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

std::filesystem::path settings_path() {
#ifdef __SWITCH__
    return "/config/kdeconnect/settings.json";
#else
    const char* home = getenv("HOME");
    const std::filesystem::path base =
        home ? std::filesystem::path(home) : std::filesystem::current_path();
    return base / ".config" / "minikdeconnect" / "settings.json";
#endif
}

std::mutex g_mutex;
std::unordered_map<uint8_t, bool>    g_bool;
std::unordered_map<uint8_t, int32_t> g_int;

bool default_bool(KdecBoolSettingKey key) {
    switch (key) {
        case KdecBoolSettingKey::NotificationShowRemote:    return true;
        case KdecBoolSettingKey::NotificationShowOnConnect: return true;
        default: return false;
    }
}

int32_t default_int(KdecIntSettingKey key) {
    switch (key) {
        case KdecIntSettingKey::NotificationDuration: return 4000;
        default: return 0;
    }
}

// Fill in defaults for any key not already present in the maps.
// Caller must hold g_mutex.
void apply_defaults_locked() {
    const int nb = static_cast<int>(KdecBoolSettingKey::KDEC_BOOL_SETTING_COUNT);
    for (int i = 0; i < nb; ++i) {
        if (!g_bool.count(static_cast<uint8_t>(i)))
            g_bool[static_cast<uint8_t>(i)] = default_bool(static_cast<KdecBoolSettingKey>(i));
    }
    const int ni = static_cast<int>(KdecIntSettingKey::KDEC_INT_SETTING_COUNT);
    for (int i = 0; i < ni; ++i) {
        if (!g_int.count(static_cast<uint8_t>(i)))
            g_int[static_cast<uint8_t>(i)] = default_int(static_cast<KdecIntSettingKey>(i));
    }
}

} // namespace

namespace SettingsStore {

void load() {
    std::lock_guard lock(g_mutex);

    const auto path = settings_path();
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            std::string buf(static_cast<size_t>(sz), '\0');
            fread(buf.data(), 1, static_cast<size_t>(sz), f);
            fclose(f);
            const auto j = nlohmann::json::parse(buf, nullptr, false);
            if (!j.is_discarded()) {
                if (j.contains("bool") && j["bool"].is_object()) {
                    for (auto& [k, v] : j["bool"].items()) {
                        if (v.is_boolean()) {
                            try { g_bool[static_cast<uint8_t>(std::stoi(k))] = v.get<bool>(); }
                            catch (...) {}
                        }
                    }
                }
                if (j.contains("int") && j["int"].is_object()) {
                    for (auto& [k, v] : j["int"].items()) {
                        if (v.is_number_integer()) {
                            try { g_int[static_cast<uint8_t>(std::stoi(k))] = v.get<int32_t>(); }
                            catch (...) {}
                        }
                    }
                }
            }
        } else {
            fclose(f);
        }
    }

    apply_defaults_locked();
}

void save() {
    // Copy under lock, then write outside lock so we don't hold it during I/O.
    std::unordered_map<uint8_t, bool>    bool_copy;
    std::unordered_map<uint8_t, int32_t> int_copy;
    {
        std::lock_guard lock(g_mutex);
        bool_copy = g_bool;
        int_copy  = g_int;
    }

    nlohmann::json j;
    j["bool"] = nlohmann::json::object();
    j["int"]  = nlohmann::json::object();
    for (const auto& [k, v] : bool_copy) j["bool"][std::to_string(k)] = v;
    for (const auto& [k, v] : int_copy)  j["int"][std::to_string(k)]  = v;

    const auto path = settings_path();
    std::filesystem::create_directories(path.parent_path());
    FILE* f = fopen(path.c_str(), "wb");
    if (f) {
        const std::string data = j.dump(2);
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
    } else {
        Logger::error("SettingsStore: failed to write " + path.string());
    }
}

bool get(KdecBoolSettingKey key) {
    std::lock_guard lock(g_mutex);
    const auto it = g_bool.find(static_cast<uint8_t>(key));
    return it != g_bool.end() ? it->second : default_bool(key);
}

void set(KdecBoolSettingKey key, bool value) {
    {
        std::lock_guard lock(g_mutex);
        g_bool[static_cast<uint8_t>(key)] = value;
    }
    save();
}

int32_t get(KdecIntSettingKey key) {
    std::lock_guard lock(g_mutex);
    const auto it = g_int.find(static_cast<uint8_t>(key));
    return it != g_int.end() ? it->second : default_int(key);
}

void set(KdecIntSettingKey key, int32_t value) {
    {
        std::lock_guard lock(g_mutex);
        g_int[static_cast<uint8_t>(key)] = value;
    }
    save();
}

std::vector<KdecWireSettingEntry> get_all() {
    std::lock_guard lock(g_mutex);
    std::vector<KdecWireSettingEntry> result;
    result.reserve(g_bool.size() + g_int.size());
    for (const auto& [k, v] : g_bool) {
        KdecWireSettingEntry e{};
        e.key           = k;
        e.value.as_bool = v;
        result.push_back(e);
    }
    for (const auto& [k, v] : g_int) {
        KdecWireSettingEntry e{};
        e.key          = k;
        e.value.as_int = v;
        result.push_back(e);
    }
    return result;
}

} // namespace SettingsStore
