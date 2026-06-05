#pragma once

#ifdef DEBUG_HEAP

#include <malloc.h>
#include "logger.h"

struct MemStats {
    size_t proc_used_kb;
    size_t heap_used_kb;
    size_t heap_total_kb;
    size_t heap_peak_kb;
    size_t heap_total_peak_kb;
};

static size_t s_heap_peak = 0;
static size_t s_heap_total_peak = 0;

static MemStats get_mem_stats() {
    MemStats s{};

    // Kernel-reported process memory
    u64 used = 0;
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    s.proc_used_kb  = used / 1024;

    // Heap allocator stats from newlib malloc
    struct mallinfo mi = mallinfo();
    // mallinfo fields are int; cast via unsigned int to avoid sign issues
    size_t heap_used = static_cast<unsigned int>(mi.uordblks);
    size_t heap_free = static_cast<unsigned int>(mi.fordblks);
    s.heap_used_kb  = heap_used / 1024;
    s.heap_total_kb = (heap_used + heap_free) / 1024;

    if (heap_used > s_heap_peak) s_heap_peak = heap_used;
    if (heap_used + heap_free > s_heap_total_peak) s_heap_total_peak = heap_used + heap_free;
    s.heap_peak_kb = s_heap_peak / 1024;
    s.heap_total_peak_kb = s_heap_total_peak / 1024;
    return s;
}

static void log_mem_stats() {
    const auto mem = get_mem_stats();
    Logger::info("Heap: %4zu KB used (peak %4zu KB) / %4zu KB arena (peak %4zu KB) (total RAM used %4zu KB)",
        mem.heap_used_kb, mem.heap_peak_kb, mem.heap_total_kb, mem.heap_total_peak_kb, mem.proc_used_kb);
}

#endif