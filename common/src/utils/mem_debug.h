#pragma once

#ifdef MEM_DEBUG

#include <malloc.h>
#include <new>

struct MemStats {
    u64    proc_used_mb;
    u64    proc_total_mb;
    size_t heap_used_kb;
    size_t heap_total_kb;
    size_t heap_peak_kb;
};

static size_t s_heap_peak = 0;

static MemStats get_mem_stats() {
    MemStats s{};

    // Kernel-reported process memory
    u64 used = 0, total = 0;
    svcGetInfo(&used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    s.proc_used_mb  = used  / (1024ULL * 1024ULL);
    s.proc_total_mb = total / (1024ULL * 1024ULL);

    // Heap allocator stats from newlib malloc
    struct mallinfo mi = mallinfo();
    // mallinfo fields are int; cast via unsigned int to avoid sign issues
    size_t heap_used = static_cast<size_t>(static_cast<unsigned int>(mi.uordblks));
    size_t heap_free = static_cast<size_t>(static_cast<unsigned int>(mi.fordblks));
    s.heap_used_kb  = heap_used / 1024;
    s.heap_total_kb = (heap_used + heap_free) / 1024;

    if (heap_used > s_heap_peak) s_heap_peak = heap_used;
    s.heap_peak_kb = s_heap_peak / 1024;

    return s;
}

#endif