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

// ---------------------------------------------------------------------------
// Allocation tracker  (operator new/delete overrides)
//
// Every allocation gets a 16-byte header that stores its size.  Atomic
// counters are updated on every alloc/free — no locks, no external libs.
// The 16-byte header keeps the returned pointer 16-byte aligned (malloc
// on aarch64 returns 16-byte aligned blocks).
// ---------------------------------------------------------------------------

static constexpr size_t kAllocHdr = 16; // header size; keeps 16-byte alignment

static std::atomic<size_t> s_at_live_bytes  {0};
static std::atomic<size_t> s_at_peak_bytes  {0};
static std::atomic<size_t> s_at_live_allocs {0};
static std::atomic<size_t> s_at_total_allocs{0};

static inline void at_record_alloc(size_t sz) noexcept {
    s_at_total_allocs.fetch_add(1, std::memory_order_relaxed);
    s_at_live_allocs.fetch_add(1, std::memory_order_relaxed);
    size_t live = s_at_live_bytes.fetch_add(sz, std::memory_order_relaxed) + sz;
    // CAS loop to update peak only when live exceeds it
    size_t peak = s_at_peak_bytes.load(std::memory_order_relaxed);
    while (live > peak &&
           !s_at_peak_bytes.compare_exchange_weak(
               peak, live, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

static inline void at_record_free(size_t sz) noexcept {
    s_at_live_allocs.fetch_sub(1, std::memory_order_relaxed);
    s_at_live_bytes.fetch_sub(sz, std::memory_order_relaxed);
}

// --- new / new[] ---

void* operator new(size_t size) {
    void* raw = malloc(size + kAllocHdr);
    if (!raw) return nullptr;
    *static_cast<size_t*>(raw) = size;
    at_record_alloc(size);
    return static_cast<char*>(raw) + kAllocHdr;
}

void* operator new[](size_t size) {
    void* raw = malloc(size + kAllocHdr);
    if (!raw) return nullptr;
    *static_cast<size_t*>(raw) = size;
    at_record_alloc(size);
    return static_cast<char*>(raw) + kAllocHdr;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    void* raw = malloc(size + kAllocHdr);
    if (!raw) return nullptr;
    *static_cast<size_t*>(raw) = size;
    at_record_alloc(size);
    return static_cast<char*>(raw) + kAllocHdr;
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    void* raw = malloc(size + kAllocHdr);
    if (!raw) return nullptr;
    *static_cast<size_t*>(raw) = size;
    at_record_alloc(size);
    return static_cast<char*>(raw) + kAllocHdr;
}

// --- delete / delete[] ---
// Always read size from our header; ignore the compiler-supplied size in the
// sized-delete variants so both paths share the same logic.

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    void* raw = static_cast<char*>(ptr) - kAllocHdr;
    at_record_free(*static_cast<size_t*>(raw));
    free(raw);
}

void operator delete[](void* ptr) noexcept {
    if (!ptr) return;
    void* raw = static_cast<char*>(ptr) - kAllocHdr;
    at_record_free(*static_cast<size_t*>(raw));
    free(raw);
}

void operator delete(void* ptr, size_t) noexcept {
    if (!ptr) return;
    void* raw = static_cast<char*>(ptr) - kAllocHdr;
    at_record_free(*static_cast<size_t*>(raw));
    free(raw);
}

void operator delete[](void* ptr, size_t) noexcept {
    if (!ptr) return;
    void* raw = static_cast<char*>(ptr) - kAllocHdr;
    at_record_free(*static_cast<size_t*>(raw));
    free(raw);
}

struct AllocStats {
    size_t live_kb;
    size_t peak_kb;
    size_t live_allocs;
    size_t total_allocs;
};

static AllocStats get_alloc_stats() {
    return {
        s_at_live_bytes.load(std::memory_order_relaxed)   / 1024,
        s_at_peak_bytes.load(std::memory_order_relaxed)   / 1024,
        s_at_live_allocs.load(std::memory_order_relaxed),
        s_at_total_allocs.load(std::memory_order_relaxed),
    };
}

#endif