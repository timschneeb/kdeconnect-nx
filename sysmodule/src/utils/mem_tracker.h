#pragma once

#include "config.h"

#if defined(DEBUG) && defined(DEBUG_ALLOC_TRACE)
#include <atomic>

// Live-allocation histogram + binary trace file, tracked via --wrap hooks.
// Only active in debug builds; zero overhead in release.
//
// Binary trace format v2 (little-endian, written to sdmc:/atmosphere/logs/kdec_memtrace.bin):
//   Header (16 bytes): magic "KDMT" (4) + version u8(2) + pad(3) + anchor u64(8)
//   Per record:
//     uint8_t  type;       'A' = alloc, 'F' = free
//     uint8_t  nframes;    number of PC values that follow
//     uint32_t size;       malloc_usable_size in bytes
//     uint64_t ptr;        heap address
//     uint64_t ts;         ARM CNTPCT_EL0 tick at alloc/free time (19.2 MHz)
//     uint64_t frames[];   nframes PC values, innermost first
//
// Size buckets: <=16, <=64, <=256, <=1K, <=4K, <=16K, <=64K, <=256K, >256K
namespace MemTracker {
    constexpr int kBuckets = 9;
    extern std::atomic<int32_t> live_count[kBuckets];
    extern std::atomic<int64_t> live_bytes[kBuckets];

    // Open the binary trace file. Call once early in main, before significant
    // allocations. Passing nullptr disables file tracing (histogram still works).
    void init(const char* path = "sdmc:/atmosphere/logs/kdec_memtrace.bin");
    void close_trace();

    // Logs mallinfo fragmentation stats + per-bucket live counts via Logger::info.
    void log_histogram();
}
#endif // DEBUG
