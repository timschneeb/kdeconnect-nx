#pragma once

#include <cstdio>
#include <functional>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>

#include "nxlink_sink.h"

class Logger {
public:
    // Sink is called in addition to the other outputs.
    // Set once before starting any threads. Pass nullptr to reset.
    using Sink = std::function<void(const std::string& level, const std::string& msg)>;
    static void set_nxlink_host(const std::string &host_address_str, uint16_t port = NxLink::kDefaultPort);
    static bool connect_nxlink();
    static void set_custom_sink(Sink sink);
    static void open_log_file(const char* name);
    static void shutdown();

    static void log(const std::string& level, const std::string& msg);
    static void log(const std::string& level, const std::string& msg, const std::source_location& loc);
    static void info(const std::string& msg, const std::source_location& loc = std::source_location::current()) {
        log("I", msg, loc);
    }
    static void warn(const std::string& msg, const std::source_location& loc = std::source_location::current()) {
        log("W", msg, loc);
    }
    static void error(const std::string& msg, const std::source_location& loc = std::source_location::current()) {
        log("E", msg, loc);
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void info(const char* fmt, Args&&... args) {
        info(format(fmt, std::forward<Args>(args)...), std::source_location::current());
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void warn(const char* fmt, Args&&... args) {
        warn(format(fmt, std::forward<Args>(args)...), std::source_location::current());
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void error(const char* fmt, Args&&... args) {
        error(format(fmt, std::forward<Args>(args)...), std::source_location::current());
    }

    template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
    static void log(const std::string& level, const char* fmt, Args&&... args) {
        log(level, format(fmt, std::forward<Args>(args)...), std::source_location::current());
    }

private:
    static Sink sink_;
    static NxLink nxlink_;
    static FILE* log_file_;

    static std::string format(const char* fmt, ...);
};
