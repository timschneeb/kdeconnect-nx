#include "logger.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

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
}

void Logger::info(const std::string& msg) {
    log("INFO", msg);
}

void Logger::warn(const std::string& msg) {
    log("WARN", msg);
}

void Logger::error(const std::string& msg) {
    log("ERROR", msg);
}

void Logger::log(const std::string& level, const std::string& msg) {
    std::cerr << "[" << now_string() << "] " << level << ": " << msg << "\n";
}
