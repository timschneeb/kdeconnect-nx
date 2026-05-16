#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "logger.h"

// Size of the inner heap (adjust as necessary).
#define INNER_HEAP_SIZE 0x80000

#ifdef __cplusplus
extern "C" {
#endif

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Sysmodules will normally only want to use one FS session.
u32 __nx_fs_num_sessions = 1;

extern void __libnx_init_time(void);

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

// Service initialization.
void __appInit(void)
{
    Result rc;

    // Open a service manager session.
    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    // Retrieve the current version of Horizon OS.
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    // Enable this if you want to use HID.
    /*rc = hidInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));*/

    // Enable this if you want to use time.
    rc = timeInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));

    __libnx_init_time();

    // Disable this if you don't want to use the filesystem.
    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    // Disable this if you don't want to use the SD card filesystem.
    fsdevMountSdmc();

    // Add other services you want to use here.

    // Close the service manager session.
    smExit();
}

// Service deinitialization.
void __appExit(void)
{
    // Close extra services you added to __appInit here.
    fsdevUnmountAll();
    fsExit();
    timeExit();
    //hidExit(); // Enable this if you want to use HID.
}

#ifdef __cplusplus
}
#endif

alignas(16) u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
__attribute__((weak)) u32 __nx_exception_ignoredebug = 1;

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    Logger::error("Crashed with error 0x%x\n", ctx->error_desc);

    for (int i = 0; i < 29; i++) {
        Logger::error("[X%d]: 0x%lx\n", i, ctx->cpu_gprs[i].x);
    }
    Logger::error("fp: 0x%lx\n", ctx->fp.x);
    Logger::error("lr: 0x%lx\n", ctx->lr.x);
    Logger::error("sp: 0x%lx\n", ctx->sp.x);
    Logger::error("pc: 0x%lx\n", ctx->pc.x);

    Logger::error("pstate: 0x%x\n", ctx->pstate);
    Logger::error("afsr0: 0x%x\n", ctx->afsr0);
    Logger::error("afsr1: 0x%x\n", ctx->afsr1);
    Logger::error("esr: 0x%x\n", ctx->esr);

    Logger::error("far: 0x%lx\n", ctx->far.x);
}


// Main program entrypoint
int main(int argc, char* argv[])
{
    // Initialization code can go here.

    // Your code / main loop goes here.
    // If you need threads, you can use threadCreate etc.

    // Deinitialization and resources clean up code can go here.
    return 0;
}