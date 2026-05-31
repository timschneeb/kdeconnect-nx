#include "logger.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifndef NO_LOG
Logger::Sink Logger::sink_;
FILE* Logger::log_file_ = nullptr;
#endif
#if !defined(NO_LOG) && defined(NXLINK_ENABLED)
NxLink Logger::nxlink_;
#endif

void Logger::set_nxlink_host(const std::string& host_address_str, uint16_t port) {
#if !defined(NO_LOG) && defined(NXLINK_ENABLED)
    if (host_address_str.empty()) {
        nxlink_.setHost(std::nullopt, port);
        return;
    }

    in_addr host_addr{};
    inet_pton(AF_INET, host_address_str.c_str(), &host_addr.s_addr);
    nxlink_.setHost(host_addr, port);
#endif
}

bool Logger::connect_nxlink() {
#if !defined(NO_LOG) && defined(NXLINK_ENABLED)
    return nxlink_.connectToHost() >= 0;
#else
    return false;
#endif
}

void Logger::set_custom_sink(Sink sink) {
#ifndef NO_LOG
    sink_ = std::move(sink);
#endif
}

void Logger::open_log_file(const char* name) {
#ifndef NO_LOG
    if (!name) return;

    constexpr auto dir = "sdmc:/atmosphere/logs/";
    mkdir(dir, 0777);

    log_file_ = fopen((std::string(dir) + name + ".log").c_str(), "a");
    if (log_file_)
        fputs("======================\n", log_file_);
#endif
}

void Logger::shutdown() {
#if !defined(NO_LOG) && defined(NXLINK_ENABLED)
    nxlink_.shutdown();
#endif
#ifndef NO_LOG
    sink_ = nullptr;
    if (log_file_) {
        fclose(log_file_);
        log_file_ = nullptr;
    }
#endif
}

namespace {
[[gnu::format(printf, 1, 0)]]
std::string vformat(const char* fmt, va_list args) {
#ifndef NO_LOG
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
#else
    return {};
#endif
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
#ifndef NO_LOG
    char time_buf[9];
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
    // Avoid strftime to avoid pulling in tons of locale/parsing stuff
    int h = (secs / 3600) % 24, m = (secs / 60) % 60, s = secs % 60;
    time_buf[0]='0'+h/10; time_buf[1]='0'+h%10; time_buf[2]=':';
    time_buf[3]='0'+m/10; time_buf[4]='0'+m%10; time_buf[5]=':';
    time_buf[6]='0'+s/10; time_buf[7]='0'+s%10; time_buf[8]='\0';

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
#ifdef NXLINK_ENABLED
    nxlink_.write(out.c_str());
#endif
#endif
}
