#pragma once
#include <unistd.h>

struct ScopedFd {
    int raw = -1;
    explicit ScopedFd(int fd) : raw(fd) {}
    ~ScopedFd() { if (raw >= 0) close(raw); }
    ScopedFd(ScopedFd&& o) noexcept : raw(o.raw) { o.raw = -1; }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    explicit operator bool() const { return raw >= 0; }
    int release() { int r = raw; raw = -1; return r; }
};
