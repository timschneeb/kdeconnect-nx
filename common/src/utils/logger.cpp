#include "logger.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <sys/stat.h>
#include <vector>

Logger::Sink Logger::sink_;
NxLink Logger::nxlink_;
FILE* Logger::log_file_ = nullptr;

bool Logger::connect_nxlink(const std::optional<in_addr> &host_address) {
    return nxlink_.connectToHost(host_address) >= 0;
}

void Logger::set_custom_sink(Sink sink) {
    sink_ = std::move(sink);
}

void Logger::open_log_file(const char* name) {
    if (!name) return;

    constexpr auto dir = "sdmc:/atmosphere/logs/";
    mkdir(dir, 0777);

    log_file_ = fopen((std::string(dir) + name + ".log").c_str(), "a");
    if (log_file_)
        fputs("======================\n", log_file_);
}

void Logger::shutdown() {
    sink_ = nullptr;
    nxlink_.shutdown();
    if (log_file_) {
        fclose(log_file_);
        log_file_ = nullptr;
    }
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
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
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
    auto formatted = "[" + now + "][" + level + "] " + msg + "\n";
    if (sink_) {
        sink_(level, msg);
    }
    if (log_file_) {
        fputs(formatted.c_str(), log_file_);
        fflush(log_file_);
    }
    nxlink_.write(formatted.c_str());
}
