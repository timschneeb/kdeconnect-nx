#include "logger.h"

#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

Logger::Sink Logger::sink_;
NxLink Logger::nxlink_;

bool Logger::connect_nxlink(const std::optional<in_addr> &host_address) {
    return nxlink_.connectToHost(host_address) >= 0;
}

void Logger::set_sink(Sink sink) {
    sink_ = std::move(sink);
}

void Logger::shutdown() {
    sink_ = nullptr;
    nxlink_.shutdown();
}

std::string Logger::format(const char* fmt, ...) {
    if (!fmt) {
        return {};
    }

    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    const int size = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    if (size <= 0) {
        va_end(args);
        return {};
    }

    std::vector<char> buffer(static_cast<size_t>(size) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);

    return std::string(buffer.data(), static_cast<size_t>(size));
}

namespace {
std::string now_string() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S");
    return oss.str();
}

std::string extract_class_name(std::string_view function) {
    if (function.empty()) return {};
    const auto paren = function.find('(');
    if (paren != std::string_view::npos) {
        function = function.substr(0, paren);
    }
    const auto scope = function.rfind("::");
    if (scope == std::string_view::npos) {
        return {};
    }
    const auto start = function.rfind(' ', scope);
    const size_t name_start = (start == std::string_view::npos) ? 0 : start + 1;
    return std::string(function.substr(name_start, scope - name_start));
}

std::string with_context(const std::string& msg, const std::source_location& loc) {
    const std::string cls = extract_class_name(loc.function_name());
    if (!cls.empty()) {
        return cls + ": " + msg;
    }
    return msg;
}
}

void Logger::log(const std::string& level, const std::string& msg,
                 const std::source_location& loc) {
    log(level, with_context(msg, loc));
}

void Logger::log(const std::string& level, const std::string& msg) {
    auto now = now_string();
    if (sink_) {
        sink_(level, msg);
    } else {
        std::fprintf(stderr, "[%s] %s: %s\n", now.c_str(), level.c_str(), msg.c_str());
    }
    nxlink_.write(("[" + now + "][" + level + "] " + msg + "\n").c_str());
}
