#pragma once

#ifdef DEBUG_SOCKETS

#include <cstring>
#include <mutex>
#include "logger.h"

namespace SocketStats {

constexpr int kMaxTracked = 32;
struct Slot { int fd = -1; const char* name = nullptr; };

inline std::mutex s_mutex;
inline Slot       s_slots[kMaxTracked];
inline int        s_peak  = 0;
inline uint32_t   s_total = 0;

inline void on_open(const int fd, const char* name) {
    std::lock_guard lock(s_mutex);
    ++s_total;
    for (auto& slot : s_slots) {
        if (slot.fd < 0) { slot = {fd, name}; break; }
    }
    int n = 0;
    for (const auto& slot : s_slots) if (slot.fd >= 0) ++n;
    if (n > s_peak) s_peak = n;
}

inline void on_close(const int fd) {
    std::lock_guard lock(s_mutex);
    for (auto& slot : s_slots) {
        if (slot.fd == fd) { slot = {-1, nullptr}; return; }
    }
}

inline void log(const int pending_threads, const int sessions) {
    std::lock_guard lock(s_mutex);

    int open = 0;
    for (const auto& slot : s_slots) if (slot.fd >= 0) ++open;

    // Aggregate per-name counts into a compact string "[name:n ...]"
    struct NC { const char* name; int n; };
    NC nc[kMaxTracked]{};
    int nc_len = 0;
    for (const auto& slot : s_slots) {
        if (slot.fd < 0) continue;
        bool found = false;
        for (int i = 0; i < nc_len; ++i) {
            if (strcmp(nc[i].name, slot.name) == 0) { ++nc[i].n; found = true; break; }
        }
        if (!found && nc_len < kMaxTracked) nc[nc_len++] = {slot.name, 1};
    }

    char names[192] = {};
    int pos = 0;
    for (int i = 0; i < nc_len && pos < static_cast<int>(sizeof(names)) - 1; ++i) {
        pos += snprintf(names + pos, sizeof(names) - pos,
                        "%s%s:%d", i ? " " : "", nc[i].name, nc[i].n);
    }

    Logger::info("Sockets: %d open (peak %d, %u total) [%s] | transient threads: %d | sessions: %d",
                 open, s_peak, s_total, names, pending_threads, sessions);
}

} // namespace SocketStats

#endif // DEBUG_SOCKETS
