#pragma once

#include <functional>
#include <string>

class Logger {
public:
    // Called in addition to (or instead of) the default stderr output.
    // Set once before starting any threads. Pass nullptr to reset.
    using Sink = std::function<void(const std::string& level, const std::string& msg)>;
    static void set_sink(Sink sink);

    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void log(const std::string& level, const std::string& msg);

private:
    static Sink sink_;
};
