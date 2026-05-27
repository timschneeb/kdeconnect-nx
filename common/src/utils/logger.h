#pragma once

#include <cstdio>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "nxlink_sink.h"

class Logger {
public:
    // Sink is called in addition to the other outputs.
    // Set once before starting any threads. Pass nullptr to reset.
    using Sink = std::function<void(std::string_view level, std::string_view msg)>;
    static void set_nxlink_host(const std::string& host_address_str, uint16_t port = NxLink::kDefaultPort);
    [[nodiscard]] static bool connect_nxlink();
    static void set_custom_sink(Sink sink);
    static void open_log_file(const char* name);
    static void shutdown();

    static void log(std::string_view level, std::string_view msg);
    static void log(std::string_view level, std::string_view msg, const std::source_location &loc);
    static void info(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
        log("I", msg, loc);
    }
    static void warn(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
        log("W", msg, loc);
    }
    static void error(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
        log("E", msg, loc);
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    [[gnu::format(printf, 1, 2)]]
    static void info(const char* fmt, Args&&... args) {
        log("I", format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    [[gnu::format(printf, 1, 2)]]
    static void warn(const char* fmt, Args&&... args) {
        log("W", format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    [[gnu::format(printf, 1, 2)]]
    static void error(const char* fmt, Args&&... args) {
        log("E", format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    [[gnu::format(printf, 2, 3)]]
    static void log(std::string_view level, const char* fmt, Args&&... args) {
        log(level, format(fmt, std::forward<Args>(args)...));
    }

private:
    static Sink sink_;
    static NxLink nxlink_;
    static FILE* log_file_;

    [[gnu::format(printf, 1, 2)]]
    static std::string format(const char* fmt, ...);
};