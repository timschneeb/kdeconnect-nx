#pragma once
#include <optional>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

#ifdef __SWITCH__
#include <switch/runtime/nxlink.h>
#else
struct in_addr;
static struct in_addr __nxlink_host;
#define NXLINK_CLIENT_PORT 28771
#endif

class NxLink {
public:
    static constexpr uint16_t kDefaultPort = NXLINK_CLIENT_PORT;

    ~NxLink();
    void setHost(const std::optional<in_addr>& host_address, uint16_t port = kDefaultPort);
    int connectToHost();
    bool isEnabled() const;
    void write(const char* message);
    void shutdown();
private:
    void reconnectAndReplay();

    static constexpr size_t MAX_CACHE_SIZE = 200;
    int sock_ = -1;
    uint16_t port_ = kDefaultPort;
    std::optional<in_addr> host_address_ = std::nullopt;
    std::queue<std::string> message_cache_;
    std::mutex mutex_;
    bool reconnect_in_progress_ = false;
    std::thread reconnect_thread_;
    std::atomic<bool> shutting_down_ = false;
};
