#include "nxlink_sink.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <optional>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

void NxLink::setHost(const std::optional<in_addr> &host_address, uint16_t port) {

#ifdef NXLINK_ENABLED
    host_address_ = host_address;
    port_ = port;
#endif
}

int NxLink::connectToHost()
{
#ifdef NXLINK_ENABLED
    if (!isEnabled()) {
        errno = ENETUNREACH;
        return -1;
    }

    sockaddr_in srv_addr{};

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) {
        return -1;
    }

    // set to non-blocking
    int flags = fcntl(sock_, F_GETFL, 0);
    if (flags == -1) {
        close(sock_);
        return -1;
    }

    if (fcntl(sock_, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(sock_);
        return -1;
    }

    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr = host_address_.value_or(__nxlink_host);
    srv_addr.sin_port = htons(port_);

    int ret = connect(sock_, reinterpret_cast<struct sockaddr *>(&srv_addr), sizeof(srv_addr));
    if (ret != 0 && errno != EINPROGRESS) {
        close(sock_);
        sock_ = -1;
        return -1;
    }

    if (ret != 0) { // EINPROGRESS
        pollfd pfd{};

        pfd.fd      = sock_;
        pfd.events  = POLLOUT;
        pfd.revents = 0;

        int n = poll(&pfd, 1, 1000); // only wait up to 1s to connect
        if (n < 0) {
            close(sock_);
            sock_ = -1;
            return -1;
        }

        if (n == 0 || !(pfd.revents & POLLOUT)) {
            close(sock_);
            sock_ = -1;
            errno = ETIMEDOUT;
            return -1;
        }
    }

    // reset back to blocking
    if (fcntl(sock_, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        close(sock_);
        sock_ = -1;
        return -1;
    }

    return sock_;
#else
    return -1;
#endif
}

bool NxLink::isEnabled() const {
#ifdef NXLINK_ENABLED
    return __nxlink_host.s_addr || host_address_.has_value();
#else
    return false;
#endif
}

void NxLink::reconnectAndReplay()
{
#ifdef NXLINK_ENABLED
    // Keep retrying until connected or shutting down
    while (!shutting_down_) {
        if (connectToHost() >= 0) {
            std::lock_guard lock(mutex_);
            if (shutting_down_) break;
            while (!message_cache_.empty()) {
                const auto& cached_msg = message_cache_.front();
                if (::write(sock_, cached_msg.c_str(), cached_msg.length()) >= 0) {
                    message_cache_.pop();
                } else {
                    close(sock_);
                    sock_ = -1;
                    break;
                }
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    reconnect_in_progress_.store(false, std::memory_order_release);
#endif
}

NxLink::~NxLink()
{
    shutdown();
}

void NxLink::shutdown()
{
#ifdef NXLINK_ENABLED
    shutting_down_ = true;
    // Wait for any in-progress reconnect to observe shutting_down_ and exit.
    // connectToHost() has at most a 1s poll timeout, so this completes quickly.
    while (reconnect_in_progress_.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
#endif
}

void NxLink::write(const char* message)
{
#ifdef NXLINK_ENABLED
    if (!message || !isEnabled() || shutting_down_) {
        return;
    }

    std::lock_guard lock(mutex_);

    if (sock_ < 0 && isEnabled()) {
        if (message_cache_.size() < MAX_CACHE_SIZE) {
            message_cache_.emplace(message);
        } else if (message_cache_.size() == MAX_CACHE_SIZE) {
            message_cache_.emplace("[WARNING] Message cache overflow, dropping messages until reconnect.\n");
        }

        if (!reconnect_in_progress_.load()) {
            reconnect_in_progress_.store(true);
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 16 * 1024);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_t tid;
            pthread_create(&tid, &attr, [](void* self) -> void* {
                static_cast<NxLink*>(self)->reconnectAndReplay();
                return nullptr;
            }, this);
            pthread_attr_destroy(&attr);
        }
    } else if (sock_ >= 0) {
        if (::write(sock_, message, std::strlen(message)) < 0) {
            close(sock_);
            sock_ = -1;
        }
    }
#endif
}
