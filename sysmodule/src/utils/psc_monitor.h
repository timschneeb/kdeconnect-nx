#pragma once
#include <switch.h>
#include <atomic>

class PscMonitor {
public:
    ~PscMonitor() { stop(); }
    void start();
    void stop();
    static bool is_awake();

private:
    static void thread_func(void* arg);

    std::atomic<bool> running_{false};
    Thread thread_{};
    PscPmModule module_{};
};
