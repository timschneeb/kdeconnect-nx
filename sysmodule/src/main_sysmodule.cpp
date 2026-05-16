#include <switch.h>
#include <stratosphere.hpp>

#include "nx_application.h"
#include "utils/logger.h"

namespace ams {

    ncm::ProgramId CurrentProgramId = {0x4DE000000C011EC7};

    namespace result {
        bool CallFatalOnResultAssertion = true;
    }

    namespace {

        alignas(0x40) constinit u8 g_heap_memory[128_KB];
        constinit lmem::HeapHandle g_heap_handle;
        constinit bool g_heap_initialized;
        constinit os::SdkMutex g_heap_init_mutex;

        lmem::HeapHandle GetHeapHandle() {
            if (AMS_UNLIKELY(!g_heap_initialized)) {
                std::scoped_lock lk(g_heap_init_mutex);
                if (AMS_LIKELY(!g_heap_initialized)) {
                    g_heap_handle = lmem::CreateExpHeap(g_heap_memory, sizeof(g_heap_memory), lmem::CreateOption_ThreadSafe);
                    g_heap_initialized = true;
                }
            }
            return g_heap_handle;
        }

        void *Allocate(size_t size) {
            return lmem::AllocateFromExpHeap(GetHeapHandle(), size);
        }

        void *AllocateWithAlign(size_t sz, size_t align) {
            return lmem::AllocateFromExpHeap(GetHeapHandle(), sz, align);
        }

        void Deallocate(void *p, size_t size) {
            AMS_UNUSED(size);
            return lmem::FreeToExpHeap(GetHeapHandle(), p);
        }

    } // namespace

    namespace init {

        void InitializeSystemModule() {
            R_ABORT_UNLESS(sm::Initialize());
            R_ABORT_UNLESS(timeInitialize());
            R_ABORT_UNLESS(fsInitialize());
            R_ABORT_UNLESS(fsdevMountSdmc());
            R_ABORT_UNLESS(socketInitializeDefault());
            R_ABORT_UNLESS(nifmInitialize(NifmServiceType_System));
        }

        void FinalizeSystemModule() {
            nifmExit();
            socketExit();
            fsdevUnmountAll();
            fsExit();
            timeExit();
        }

        void Startup() {}

    } // namespace init

    void Main() {
        Logger::connect_nxlink();

        auto app = NxApplication();
        while (true) {
            app.processEvents();
            svcSleepThread(100'000'000LL);
        }
    }

} // namespace ams

void *operator new(size_t size) { return ams::Allocate(size); }
void *operator new(size_t size, const std::nothrow_t &) noexcept { return ams::Allocate(size); }
void operator delete(void *p) noexcept { return ams::Deallocate(p, 0); }
void operator delete(void *p, size_t size) noexcept { return ams::Deallocate(p, size); }
void *operator new[](size_t size) { return ams::Allocate(size); }
void *operator new[](size_t size, const std::nothrow_t &) noexcept { return ams::Allocate(size); }
void operator delete[](void *p) noexcept { return ams::Deallocate(p, 0); }
void operator delete[](void *p, size_t size) noexcept { return ams::Deallocate(p, size); }
void *operator new(size_t size, std::align_val_t align) { return ams::AllocateWithAlign(size, static_cast<size_t>(align)); }
void operator delete(void *p, std::align_val_t align) noexcept { AMS_UNUSED(align); return ams::Deallocate(p, 0); }