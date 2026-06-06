#pragma once
#include <unistd.h>
#include "config.h"
#ifdef DEBUG_SOCKETS
#include "socket_stats.h"
#endif

struct ScopedFd {
    int raw = -1;
    explicit ScopedFd(const int fd) : raw(fd) {}
    ~ScopedFd() {
        if (raw >= 0) {
#ifdef DEBUG_SOCKETS
            SocketStats::on_close(raw);
#endif
            close(raw);
        }
    }
    ScopedFd(ScopedFd&& o) noexcept : raw(o.raw) { o.raw = -1; }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    explicit operator bool() const { return raw >= 0; }
    int release() { int r = raw; raw = -1; return r; }
};
