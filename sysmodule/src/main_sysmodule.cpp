#include <switch.h>
#include <cstring>
#include <exception>
#include <stdexcept>

#include "nx_application.h"
#include "utils/logger.h"

#define MEM_DEBUG
#include "src/utils/mem_debug.h"

#define INNER_HEAP_SIZE 3'000'000 // 3MB

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
        constexpr SocketInitConfig socketInitConfig = {
            .tcp_tx_buf_size     = 32 * 1024,
            .tcp_rx_buf_size     = 32 * 1024,
            .tcp_tx_buf_max_size = 64 * 1024,
            .tcp_rx_buf_max_size = 64 * 1024,
            .udp_tx_buf_size     = 8 * 1024,
            .udp_rx_buf_size     = 16 * 1024,
#ifdef NXLINK_ENABLED
            .sb_efficiency       = 3,
#else
            .sb_efficiency       = 2,
#endif

            .bsd_service_type    = BsdServiceType_Auto
        };

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
    fsdevUnmountAll();
    fsExit();
    timeExit();
    smExit();
}
}

int main(int argc, char* argv[])
{
    std::set_terminate([]() {
        if (auto eptr = std::current_exception()) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                Logger::error("terminate: unhandled exception: %s", e.what());
            } catch (...) {
                Logger::error("terminate: unhandled exception of unknown type");
            }
        } else {
            Logger::error("terminate: called without active exception (joinable thread destroyed?)");
        }

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
    auto app = NxApplication();
    while (true) {
        app.processEvents();

#ifndef MEM_DEBUG
        svcSleepThread(100'000'000LL);
#else
        const auto mem = get_mem_stats();
        const auto al  = get_alloc_stats();

        Logger::info("Heap : %5zu KB used / %5zu KB total  (peak %5zu KB)", mem.heap_used_kb, mem.heap_total_kb, mem.heap_peak_kb);
        Logger::info("new  : %5zu KB live  (peak %5zu KB)  allocs: %zu live / %zu total", al.live_kb, al.peak_kb, al.live_allocs, al.total_allocs);

        svcSleepThread(1'000'000'000LL);
#endif
    }
    return 0;
}