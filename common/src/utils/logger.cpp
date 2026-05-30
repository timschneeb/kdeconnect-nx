#include "logger.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>

Logger::Sink Logger::sink_;
NxLink Logger::nxlink_;
FILE* Logger::log_file_ = nullptr;

void Logger::set_nxlink_host(const std::string& host_address_str, uint16_t port) {
    if (host_address_str.empty()) {
        nxlink_.setHost(std::nullopt, port);
        return;
    }

    in_addr host_addr{};
    inet_pton(AF_INET, host_address_str.c_str(), &host_addr.s_addr);
    nxlink_.setHost(host_addr, port);
}

bool Logger::connect_nxlink() {
    return nxlink_.connectToHost() >= 0;
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

namespace {
[[gnu::format(printf, 1, 0)]]
std::string vformat(const char* fmt, va_list args) {
    if (!fmt) return {};

    char stack_buf[256];
    va_list args_copy;
    va_copy(args_copy, args);
    const int len = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, args_copy);
    va_end(args_copy);

    if (len < 0) return {};
    if (len < static_cast<int>(sizeof(stack_buf))) return {stack_buf, static_cast<size_t>(len)};

    std::string result(len, '\0');
    std::vsnprintf(result.data(), len + 1, fmt, args);
    return result;
}
} // namespace

void Logger::info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log("I", vformat(fmt, args));
    va_end(args);
}

void Logger::warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log("W", vformat(fmt, args));
    va_end(args);
}

void Logger::error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log("E", vformat(fmt, args));
    va_end(args);
}

void Logger::log(std::string_view level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(level, vformat(fmt, args));
    va_end(args);
}

void Logger::log(std::string_view level, std::string_view msg) {
    char time_buf[9];
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    strftime(time_buf, 9, "%H:%M:%S", &tm);

    // "[HH:MM:SS][L] msg\n": pre-reserve to avoid reallocations
    std::string out;
    out.reserve(12 + level.size() + msg.size());
    out = '[';
    out.append(time_buf, 8);
    out += "][";
    out += level;
    out += "] ";
    out += msg;
    out += '\n';

    if (sink_) sink_(level, msg);
    if (log_file_) {
        fputs(out.c_str(), log_file_);
        fflush(log_file_);
    }
    nxlink_.write(out.c_str());
}
