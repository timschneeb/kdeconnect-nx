#include "nxlink_sink.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <optional>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch/runtime/nxlink.h>

int NxLink::connectToHost(const std::optional<in_addr>& host_address)
{
    host_address_ = host_address;

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
    srv_addr.sin_port = htons(NXLINK_CLIENT_PORT);

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
}

bool NxLink::isEnabled() const {
    return __nxlink_host.s_addr || host_address_.has_value();
}

void NxLink::reconnectAndReplay()
{
    if (connectToHost(host_address_) >= 0) {
        // Successfully reconnected, replay cached messages
        std::lock_guard lock(mutex_);
        while (!message_cache_.empty()) {
            const auto& cached_msg = message_cache_.front();
            if (::write(sock_, cached_msg.c_str(), cached_msg.length()) >= 0) {
                message_cache_.pop();
            } else {
                // Write failed, close socket and stop replaying
                close(sock_);
                sock_ = -1;
                break;
            }
        }
    }
    reconnect_in_progress_ = false;
}

NxLink::~NxLink()
{
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
    if (sock_ >= 0) {
        close(sock_);
    }
}

void NxLink::write(const char* message)
{
    if (!message || !isEnabled()) {
        return;
    }

    std::thread old_thread;

    {
        // Reconnect if necessary
        std::lock_guard lock(mutex_);

        if (sock_ < 0 && isEnabled()) {
            // Cache the message if we have a host address but no connection
            if (message_cache_.size() < MAX_CACHE_SIZE) {
                message_cache_.emplace(message);
            } else if (message_cache_.size() == MAX_CACHE_SIZE) {
                message_cache_.emplace("[WARNING] Message cache overflow, dropping messages until reconnect.\n");
            }

            // Start reconnect thread if not already in progress
            if (!reconnect_in_progress_) {
                reconnect_in_progress_ = true;

                // Move old thread to join outside the lock
                old_thread = std::move(reconnect_thread_);

                // Start new reconnect thread
                reconnect_thread_ = std::thread(&NxLink::reconnectAndReplay, this);
            }

            // Lock automatically released here when scope ends
        } else if (sock_ < 0) {
            return;
        } else {
            size_t len = 0;
            while (message[len] != '\0') {
                len++;
            }

            if (::write(sock_, message, len) < 0) {
                // Connection lost, reset socket
                close(sock_);
                sock_ = -1;
            }
        }
    }

    // Join outside the lock to avoid deadlock
    if (old_thread.joinable()) {
        old_thread.join();
    }
}