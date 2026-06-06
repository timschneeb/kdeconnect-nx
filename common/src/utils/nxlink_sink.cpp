#include "nxlink_sink.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

NxLink::NxLink()
{
#ifdef NXLINK_ENABLED
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024);
    pthread_create(&bg_thread_, &attr, [](void* self) -> void* {
        static_cast<NxLink*>(self)->backgroundThread();
        return nullptr; }, this);
    pthread_attr_destroy(&attr);
#endif
}

void NxLink::setHost(const std::optional<in_addr>& host_address, const uint16_t port)
{

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

    sockaddr_in srv_addr { };

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // set to non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        close(fd);
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }

    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr = host_address_.value_or(__nxlink_host);
    srv_addr.sin_port = htons(port_);

    int ret = connect(fd, reinterpret_cast<struct sockaddr*>(&srv_addr), sizeof(srv_addr));
    if (ret != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (ret != 0) { // EINPROGRESS
        pollfd pfd { };

        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int n = poll(&pfd, 1, 1000); // only wait up to 1s to connect
        if (n < 0) {
            close(fd);
            return -1;
        }

        if (n == 0 || !(pfd.revents & POLLOUT)) {
            close(fd);
            errno = ETIMEDOUT;
            return -1;
        }
    }

    // reset back to blocking
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }

    sock_ = fd;
    return fd;
#else
    return -1;
#endif
}

bool NxLink::isEnabled() const
{
#ifdef NXLINK_ENABLED
    return __nxlink_host.s_addr || host_address_.has_value();
#else
    return false;
#endif
}

void NxLink::backgroundThread()
{
#ifdef NXLINK_ENABLED
    while (!shutting_down_) {
        bool needs_connect;
        {
            std::lock_guard lock(mutex_);
            needs_connect = sock_ < 0 && isEnabled();
        }

        if (needs_connect && connectToHost() >= 0) {
            std::lock_guard lock(mutex_);
            if (!shutting_down_) {
                flushCache();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
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
    if (bg_thread_)
        pthread_join(bg_thread_, nullptr);
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
#endif
}

void NxLink::write(const char* message)
{
#ifdef NXLINK_ENABLED
    if (!message || shutting_down_) {
        return;
    }

    std::lock_guard lock(mutex_);

    if (sock_ < 0 || !isEnabled()) {
        if (message_cache_.size() < MAX_CACHE_SIZE) {
            message_cache_.emplace(message);
        } else if (message_cache_.size() == MAX_CACHE_SIZE) {
            message_cache_.emplace("[WARNING] Message cache overflow, dropping messages until reconnect.\n");
        }
    } else {
        flushCache();
        if (::write(sock_, message, std::strlen(message)) < 0) {
            close(sock_);
            sock_ = -1;
        }
    }
#endif
}

void NxLink::writeToCache(const char* message)
{
#ifdef NXLINK_ENABLED
    std::lock_guard lock(mutex_);
    if (message_cache_.size() < MAX_CACHE_SIZE) {
        message_cache_.emplace(message);
    }
#endif
}

void NxLink::flushCache()
{
#ifdef NXLINK_ENABLED
    while (!message_cache_.empty()) {
        const auto& msg = message_cache_.front();
        if (::write(sock_, msg.c_str(), msg.length()) >= 0) {
            message_cache_.pop();
        } else {
            close(sock_);
            sock_ = -1;
            break;
        }
    }
#endif
}
