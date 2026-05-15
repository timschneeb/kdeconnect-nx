#pragma once

#include <functional>
#include <string>
#include <type_traits>
#include <utility>

#include "nxlink_sink.h"

class Logger {
public:
    // Called in addition to (or instead of) the default stderr output.
    // Set once before starting any threads. Pass nullptr to reset.
    using Sink = std::function<void(const std::string& level, const std::string& msg)>;
    static bool connect_nxlink(const std::optional<in_addr>& host_address = std::nullopt);
    static void set_sink(Sink sink);
    static void shutdown();

    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void log(const std::string& level, const std::string& msg);

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void info(const char* fmt, Args&&... args) {
        log("I", format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void warn(const char* fmt, Args&&... args) {
        log("W", format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void error(const char* fmt, Args&&... args) {
        log("E", format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void log(const std::string& level, const char* fmt, Args&&... args) {
        log(level, format(fmt, std::forward<Args>(args)...));
    }

private:
    static Sink sink_;
    static NxLink nxlink_;

    static std::string format(const char* fmt, ...);
};
