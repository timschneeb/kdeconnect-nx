#pragma once
#include <optional>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

class NxLink {
public:
    int connectToHost(const std::optional<in_addr>& host_address);
    bool isEnabled() const;
    void write(const char* message);
    void shutdown();
    ~NxLink();
private:
    void reconnectAndReplay();

    static constexpr size_t MAX_CACHE_SIZE = 200;
    int sock_ = -1;
    std::optional<in_addr> host_address_ = std::nullopt;
    std::queue<std::string> message_cache_;
    std::mutex mutex_;
    bool reconnect_in_progress_ = false;
    std::thread reconnect_thread_;
    std::atomic<bool> shutting_down_ = false;
};