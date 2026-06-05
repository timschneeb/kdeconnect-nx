#ifdef DEBUG

#include "utils/mem_tracker.h"
#include "logger.h"
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>

// ReSharper disable once CppUnusedIncludeDirective
#include "../config.h"

#ifdef DEBUG_ALLOC_TRACE
static std::atomic<bool> ready = false;

namespace MemTracker {
    std::atomic<int32_t> live_count[kBuckets] = {};
    std::atomic<int64_t> live_bytes[kBuckets] = {};

    static constexpr size_t kLimits[kBuckets - 1] = {
        16, 64, 256, 1024, 4096, 16384, 65536, 262144
    };
    static constexpr const char* kLabels[kBuckets] = {
        "<=16B", "<=64B", "<=256B", "<=1KB", "<=4KB", "<=16KB", "<=64KB", "<=256KB", ">256KB"
    };

    static int bucket_for(size_t sz) {
        for (int i = 0; i < kBuckets - 1; ++i)
            if (sz <= kLimits[i]) return i;
        return kBuckets - 1;
    }

    void log_histogram() {
        struct mallinfo mi = mallinfo();
        const size_t fragmented = mi.fordblks > mi.keepcost
            ? mi.fordblks - mi.keepcost : 0;
        Logger::info("[MemTracker] arena=%zuKB used=%zuKB free=%zuKB "
                     "frag=%zuKB top=%zuKB free_chunks=%zu",
            mi.arena/1024, mi.uordblks/1024, mi.fordblks/1024,
            fragmented/1024, mi.keepcost/1024, (size_t)mi.ordblks);
        for (int i = 0; i < kBuckets; ++i) {
            const int32_t cnt = live_count[i].load(std::memory_order_relaxed);
            const int64_t byt = live_bytes[i].load(std::memory_order_relaxed);
            if (cnt > 0)
                Logger::info("[MemTracker]  %s: %d live allocs, %lldKB",
                    kLabels[i], cnt, (long long)byt / 1024);
            else if (cnt < 0)
                Logger::info("[MemTracker]  %s: %d untracked frees (orphan)",
                    kLabels[i], -cnt);
        }
    }
}

// ------------------------------------------------------------------
// Trace file: written with write() (no stdio, no heap).
// All multi-byte fields are little-endian.
// ------------------------------------------------------------------

static int g_trace_fd = -1;
static pthread_mutex_t g_write_mutex = PTHREAD_MUTEX_INITIALIZER;

// Write-back buffer: batches records so we're not doing one SD-card write per malloc.
static constexpr size_t kWBufSize = 32768;
static uint8_t  g_wbuf[kWBufSize];
static size_t   g_wbuf_pos = 0;

// Must be called with g_write_mutex held.
static void flush_locked() {
    if (g_wbuf_pos == 0 || g_trace_fd < 0) return;
    ::write(g_trace_fd, g_wbuf, g_wbuf_pos);
    g_wbuf_pos = 0;
}

// Header is 16 bytes:
//   magic(4) "KDMT" + version u8(1) + pad(3) + anchor u64
// The anchor is the runtime address of MemTracker::init itself.
// The parser compares it against the ELF symbol VMA to compute the ASLR slide
// without needing an external crash log.

void MemTracker::init(const char* path) {
    if (!path) return;

    ready = true;

    g_trace_fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_trace_fd < 0) {
        Logger::error("[MemTracker] Failed to open trace file: %s", path);
        return;
    }
    const uint8_t hdr[8] = {'K', 'D', 'M', 'T', 2, 0, 0, 0};
    ::write(g_trace_fd, hdr, sizeof(hdr));
    const uint64_t anchor = reinterpret_cast<uint64_t>(&MemTracker::init);
    ::write(g_trace_fd, &anchor, sizeof(anchor));
    Logger::info("[MemTracker] Tracing to %s (anchor=%p)", path,
                 reinterpret_cast<void*>(anchor));
}

void MemTracker::close_trace() {
    if (g_trace_fd >= 0) {
        pthread_mutex_lock(&g_write_mutex);
        flush_locked();
        pthread_mutex_unlock(&g_write_mutex);
        ::close(g_trace_fd);
        g_trace_fd = -1;
    }
}

// ------------------------------------------------------------------
// Backtrace via frame-pointer walk (AArch64 AAPCS64 frame layout).
// Requires -fno-omit-frame-pointer on all TUs so x29 is maintained.
// Uses O(1) stack space per frame.
// ------------------------------------------------------------------

static constexpr size_t kMaxFrames = 12;

static size_t capture_bt(void** buf) {
    struct Frame { Frame* fp; void* lr; };
    size_t n = 0;
    auto* fp = reinterpret_cast<Frame*>(__builtin_frame_address(0));
    while (fp && n < kMaxFrames) {
        buf[n++] = fp->lr;
        Frame* next = fp->fp;
        // Caller's frame is always at a higher address (stack grows down).
        if (!next || next <= fp) break;
        fp = next;
    }
    return n;
}

// ARM system counter: single register read, no heap
static inline uint64_t get_tick() {
    uint64_t t;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

// ------------------------------------------------------------------
// Record writer: stack-allocated buffer, single write() call.
// Layout: type(1) nframes(1) size(4) ptr(8) ts(8) frames[](8 each)
// ------------------------------------------------------------------

static void write_record(char type, void* ptr, size_t sz, uint64_t ts,
                         void* const* frames, size_t nframes) {
    const int fd = g_trace_fd;
    if (fd < 0) return;

    // Max record: 1 + 1 + 4 + 8 + 8 + 12*8 = 118 bytes
    uint8_t buf[118];
    uint8_t* p = buf;

    *p++ = static_cast<uint8_t>(type);
    *p++ = static_cast<uint8_t>(nframes);

    uint32_t sz32 = static_cast<uint32_t>(sz);
    __builtin_memcpy(p, &sz32, 4); p += 4;

    uint64_t ptr64 = reinterpret_cast<uint64_t>(ptr);
    __builtin_memcpy(p, &ptr64, 8); p += 8;

    __builtin_memcpy(p, &ts, 8); p += 8;

    for (size_t i = 0; i < nframes; ++i) {
        uint64_t f = reinterpret_cast<uint64_t>(frames[i]);
        __builtin_memcpy(p, &f, 8); p += 8;
    }

    const size_t len = static_cast<size_t>(p - buf);
    pthread_mutex_lock(&g_write_mutex);
    if (g_wbuf_pos + len > kWBufSize) flush_locked();
    __builtin_memcpy(g_wbuf + g_wbuf_pos, buf, len);
    g_wbuf_pos += len;
    pthread_mutex_unlock(&g_write_mutex);
}

// ------------------------------------------------------------------
// --wrap hooks
// ------------------------------------------------------------------

// Prevents _Unwind_Backtrace or pthread_mutex_lock from recursing back
// into our hooks if they themselves need to allocate.
static __thread bool t_in_tracker = false;
#endif

void track_alloc(void* p) {
#ifdef DEBUG_ALLOC_TRACE
    if (!p || g_trace_fd < 0 || t_in_tracker || !ready) return;
    t_in_tracker = true;
    const uint64_t ts = get_tick();
    const size_t sz = malloc_usable_size(p);
    const int b = MemTracker::bucket_for(sz);
    MemTracker::live_count[b].fetch_add(1, std::memory_order_relaxed);
    MemTracker::live_bytes[b].fetch_add(static_cast<int64_t>(sz), std::memory_order_relaxed);
    void* frames[kMaxFrames];
    const size_t nf = capture_bt(frames);
    write_record('A', p, sz, ts, frames, nf);
    t_in_tracker = false;
#endif
}

static void track_free(void* p) {
#ifdef DEBUG_ALLOC_TRACE
    if (!p || g_trace_fd < 0 || t_in_tracker || !ready) return;
    t_in_tracker = true;
    const uint64_t ts = get_tick();
    const size_t sz = malloc_usable_size(p);
    const int b = MemTracker::bucket_for(sz);
    MemTracker::live_count[b].fetch_sub(1, std::memory_order_relaxed);
    MemTracker::live_bytes[b].fetch_sub(static_cast<int64_t>(sz), std::memory_order_relaxed);
    void* frames[kMaxFrames];
    const size_t nf = capture_bt(frames);
    write_record('F', p, sz, ts, frames, nf);
    t_in_tracker = false;
#endif
}

extern "C" {
    void* __real_malloc(size_t);
    void  __real_free(void*);
    void* __real_realloc(void*, size_t);
    void* __real_calloc(size_t, size_t);
    void* __real_memalign(size_t, size_t);
    int   __real_posix_memalign(void**, size_t, size_t);
    void* __real_aligned_alloc(size_t, size_t);
    void* __real_valloc(size_t);

    void* __wrap_malloc(size_t sz) {
        void* p = __real_malloc(sz);
        track_alloc(p);
        return p;
    }

    void __wrap_free(void* p) {
        track_free(p);
        __real_free(p);
    }

    void* __wrap_realloc(void* old, size_t sz) {
        track_free(old);  // read size before realloc invalidates the block
        void* p = __real_realloc(old, sz);
        track_alloc(p);
        return p;
    }

    void* __wrap_calloc(size_t n, size_t sz) {
        void* p = __real_calloc(n, sz);
        track_alloc(p);
        return p;
    }

    void* __wrap_memalign(size_t alignment, size_t sz) {
        void* p = __real_memalign(alignment, sz);
        track_alloc(p);
        return p;
    }

    int __wrap_posix_memalign(void** memptr, size_t alignment, size_t sz) {
        int ret = __real_posix_memalign(memptr, alignment, sz);
        if (ret == 0) track_alloc(*memptr);
        return ret;
    }

    void* __wrap_aligned_alloc(size_t alignment, size_t sz) {
        void* p = __real_aligned_alloc(alignment, sz);
        track_alloc(p);
        return p;
    }

    void* __wrap_valloc(size_t sz) {
        void* p = __real_valloc(sz);
        track_alloc(p);
        return p;
    }
}

#endif // DEBUG
