#include <switch.h>
#include <cstring>
#include <exception>

// ReSharper disable once CppUnusedIncludeDirective
#include "config.h"

#include "nx_application.h"
#include "plugins/plugin_registry.h"
#include "utils/logger.h"
#include "utils/mem_debug.h"
#ifdef DEBUG_ALLOC_TRACE
#include "utils/mem_tracker.h"
#endif

#define INNER_HEAP_SIZE (2 * 1024 * 1024) // 2 MB

#define R_ABORT_UNLESS(expr) {if (Result rc = expr; R_FAILED(rc)) fatalThrow(rc);}

extern "C"
{
void __libnx_init_time(void);

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Sysmodules will normally only want to use one FS session.
u32 __nx_fs_num_sessions = 1;

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void)
{
    R_ABORT_UNLESS(smInitialize());
    {
        if (hosversionGet() == 0) {
            if (R_SUCCEEDED(setsysInitialize())) {
                SetSysFirmwareVersion fw;
                if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
                    hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
                setsysExit();
            }
        }

        R_ABORT_UNLESS(timeInitialize());
        __libnx_init_time();

        R_ABORT_UNLESS(fsInitialize());
        R_ABORT_UNLESS(fsdevMountSdmc());
        chdir("sdmc:/");

        R_ABORT_UNLESS(socketInitialize(&socketInitConfig));
        R_ABORT_UNLESS(nifmInitialize(NifmServiceType_System));
    }
}

void __appExit(void)
{
    nifmExit();
    socketExit();

#ifdef DEBUG_ALLOC_TRACE
    MemTracker::close_trace();
#endif

    fsdevUnmountAll();
    fsExit();
    timeExit();
    smExit();
}
}

#ifdef DEBUG_HEAP
static uint8_t tick = 0;
#endif

static void log_backtrace() {
    struct Frame { Frame* fp; void* lr; };
    auto* fp = reinterpret_cast<Frame*>(__builtin_frame_address(0));
    Logger::error("terminate: anchor=0x%llx symbol=log_backtrace",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&log_backtrace)));
    Logger::error("terminate: backtrace:");
    for (int i = 0; fp && i < 16; ++i) {
        Logger::error("  #%-2d  0x%llx", i, static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(fp->lr)));
        Frame* next = fp->fp;
        if (!next || next <= fp) break;
        fp = next;
    }
}

int main(int argc, char* argv[])
{
    std::set_terminate([]() {
        if (auto eptr = std::current_exception()) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                Logger::error("terminate: unhandled exception %s: %s", eptr.__cxa_exception_type()->name(), e.what());
            } catch (...) {
                Logger::error("terminate: unhandled exception %s", eptr.__cxa_exception_type()->name());
            }
        } else {
            Logger::error("terminate: called without active exception (joinable thread destroyed?)");
        }
        log_backtrace();
#ifdef DEBUG_ALLOC_TRACE
        MemTracker::close_trace();
#endif

        svcSleepThread(500'000'000LL); // give log time to flush
        // Try to kill self instead of taking down the whole system
        if (R_SUCCEEDED(pmshellInitialize())) {
            u64 id;
            if (R_SUCCEEDED(svcGetProcessId(&id, CUR_PROCESS_HANDLE))) {
                pmshellTerminateProcess(id);
                pmshellExit();
                svcSleepThread(500'000'000LL); // wait for termination
            }
        }
        std::abort();
    });

    Logger::open_log_file("kdeconnect_sysmodule");
#ifdef DEBUG_ALLOC_TRACE
    MemTracker::init();
#endif
    // Load long-lived allocations first to avoid fragmentation later on
    PluginRegistry::load_supported_types(nullptr);

    auto app = NxApplication();

#ifdef DEBUG_EXIT_TIMEOUT
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (std::chrono::steady_clock::now() < deadline) {
#else
    while (true) {
#endif
        app.processEvents();

#ifdef DEBUG_HEAP
        if (tick >= 20) {
            log_mem_stats();
            tick = 0;
        }
        tick++;
#endif
        svcSleepThread(100'000'000LL);
    }
    return 0;
}