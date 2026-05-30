#include "settings_store.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "logger.h"
#include "storage.h"

namespace {

std::string settings_path() {
#ifdef __SWITCH__
    return "/config/kdeconnect/settings.json";
#else
    const char* home = getenv("HOME");
    std::string base;
    if (home) {
        base = std::string(home) + "/.config/minikdeconnect";
    } else {
        char cwd[4096] = {};
        base = getcwd(cwd, sizeof(cwd)) ? std::string(cwd) + "/.config/minikdeconnect"
                                        : ".config/minikdeconnect";
    }
    return base + "/settings.json";
#endif
}

std::mutex g_mutex;
std::unordered_map<uint8_t, bool>    g_bool;
std::unordered_map<uint8_t, int32_t> g_int;

// --- Helpers generated from the X-macro tables in constants.h ---
// The JSON key for each setting is the stringified enum name (#n).

static const char* bool_key_name(KdecBoolSettingKey k) {
    switch (k) {
#define X(n, d) case KdecBoolSettingKey::n: return #n;
        KDEC_BOOL_SETTINGS(X)
#undef X
        default: return nullptr;
    }
}

static const char* int_key_name(KdecIntSettingKey k) {
    switch (k) {
#define X(n, d) case KdecIntSettingKey::n: return #n;
        KDEC_INT_SETTINGS(X)
#undef X
        default: return nullptr;
    }
}

static std::optional<KdecBoolSettingKey> bool_key_from_name(const std::string& s) {
#define X(n, d) if (s == #n) return KdecBoolSettingKey::n;
    KDEC_BOOL_SETTINGS(X)
#undef X
    return std::nullopt;
}

static std::optional<KdecIntSettingKey> int_key_from_name(const std::string& s) {
#define X(n, d) if (s == #n) return KdecIntSettingKey::n;
    KDEC_INT_SETTINGS(X)
#undef X
    return std::nullopt;
}

// Seed maps with compile-time defaults for any key not already present.
// Caller must hold g_mutex.
void apply_defaults_locked() {
#define X(n, d) g_bool.emplace(static_cast<uint8_t>(KdecBoolSettingKey::n), d);
    KDEC_BOOL_SETTINGS(X)
#undef X
#define X(n, d) g_int.emplace(static_cast<uint8_t>(KdecIntSettingKey::n), static_cast<int32_t>(d));
    KDEC_INT_SETTINGS(X)
#undef X
}

} // namespace

namespace SettingsStore {

void load() {
    std::lock_guard lock(g_mutex);

    // Seed with defaults first; file values will overwrite below.
    apply_defaults_locked();

    const std::string path = settings_path();
    const std::string buf = Storage::read_file(path);
    if (buf.empty()) return;

    const auto j = nlohmann::json::parse(buf, nullptr, false);
    if (j.is_discarded()) return;

    if (j.contains("bool") && j["bool"].is_object()) {
        for (const auto& [name, v] : j["bool"].items()) {
            if (!v.is_boolean()) continue;
            if (auto k = bool_key_from_name(name))
                g_bool[static_cast<uint8_t>(*k)] = v.get<bool>();
        }
    }
    if (j.contains("int") && j["int"].is_object()) {
        for (const auto& [name, v] : j["int"].items()) {
            if (!v.is_number_integer()) continue;
            if (auto k = int_key_from_name(name))
                g_int[static_cast<uint8_t>(*k)] = v.get<int32_t>();
        }
    }
}

void save() {
    // Copy under lock; do I/O outside so the lock isn't held during writes.
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
    for (const auto& [k, v] : bool_copy)
        if (const char* name = bool_key_name(static_cast<KdecBoolSettingKey>(k)))
            j["bool"][name] = v;
    for (const auto& [k, v] : int_copy)
        if (const char* name = int_key_name(static_cast<KdecIntSettingKey>(k)))
            j["int"][name] = v;

    const std::string path = settings_path();
    const auto slash = path.rfind('/');
    if (slash != std::string::npos)
        Storage::make_directories(path.substr(0, slash));
    const std::string data = j.dump(2);
    if (!Storage::write_file(path, data)) {
        Logger::error("SettingsStore: failed to write %s", path.c_str());
    }
}

bool get(KdecBoolSettingKey key) {
    std::lock_guard lock(g_mutex);
    const auto it = g_bool.find(static_cast<uint8_t>(key));
    if (it != g_bool.end()) return it->second;
    switch (key) {
#define X(n, d) case KdecBoolSettingKey::n: return (d);
        KDEC_BOOL_SETTINGS(X)
#undef X
        default: return false;
    }
}

void set(KdecBoolSettingKey key, bool value) {
    { std::lock_guard lock(g_mutex); g_bool[static_cast<uint8_t>(key)] = value; }
    save();
}

int32_t get(KdecIntSettingKey key) {
    std::lock_guard lock(g_mutex);
    const auto it = g_int.find(static_cast<uint8_t>(key));
    if (it != g_int.end()) return it->second;
    switch (key) {
#define X(n, d) case KdecIntSettingKey::n: return static_cast<int32_t>(d);
        KDEC_INT_SETTINGS(X)
#undef X
        default: return 0;
    }
}

void set(KdecIntSettingKey key, int32_t value) {
    { std::lock_guard lock(g_mutex); g_int[static_cast<uint8_t>(key)] = value; }
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
