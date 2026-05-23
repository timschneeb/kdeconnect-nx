#include <switch.h>
#include <cstring>

#include "nx_application.h"
#include "utils/logger.h"

#define INNER_HEAP_SIZE 1'000'000 // 1MB

extern "C"
{
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
}

#define R_ABORT_UNLESS(expr) {if (Result rc = expr; R_FAILED(rc)) fatalThrow(rc);}

extern "C" void __appInit(void)
{
    R_ABORT_UNLESS(smInitialize());
    {
        constexpr SocketInitConfig socketInitConfig = {
            .tcp_tx_buf_size     = 2 * 1024,
            .tcp_rx_buf_size     = 2 * 1024,
            .tcp_tx_buf_max_size = 8 * 1024,
            .tcp_rx_buf_max_size = 8 * 1024,
            .udp_tx_buf_size     = 4 * 1024,
            .udp_rx_buf_size     = 4 * 1024,
            .sb_efficiency       = 2,
            .bsd_service_type    = BsdServiceType_Auto
        };

        R_ABORT_UNLESS(timeInitialize());
        R_ABORT_UNLESS(fsInitialize());
        R_ABORT_UNLESS(fsdevMountSdmc());

        auto write_marker = [](const char* path, const char* msg) {
            FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
            fsFsCreateFile(fs, path, 0, 0);
            FsFile file;
            if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Write, &file))) return;
            fsFileWrite(&file, 0, msg, std::strlen(msg), FsWriteOption_Flush);
            fsFileClose(&file);
        };

        write_marker("/flag1", "fsdevMountSdmc OK!\n");

        R_ABORT_UNLESS(socketInitialize(&socketInitConfig));
        R_ABORT_UNLESS(nifmInitialize(NifmServiceType_System));

        write_marker("/flag2", "init OK!\n");
    }
}

extern "C" void __appExit(void)
{
    nifmExit();
    socketExit();
    fsdevUnmountAll();
    fsExit();
    timeExit();
    smExit();
}

int main(int argc, char* argv[])
{
    Logger::open_log_file("kdeconnect_sysmodule");
    auto app = NxApplication();
    while (true) {
        app.processEvents();
        svcSleepThread(100'000'000LL);
    }
    return 0;
}