#pragma once

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <pthread.h>
#include <switch/arm/tls.h>
#include <switch/kernel/thread.h>

#include "logger.h"

#define STACK_THREAD_MEASURE

// Drop-in replacement for std::thread with an explicit stack size.
// Move-only; must be joined or detached before destruction.
//
// Two constructors:
//   StackThread(size, callable, args...)          — no name
//   StackThread(size, "name", callable, args...)  — sets pthread name (≤15 chars)
//
// Build with -DSTACK_THREAD_MEASURE to enable stack painting.
// The trampoline paints the stack from the bottom up (leaving a 2 KB guard at
// the top) using 0xCD, then dump_meminfo() / join() scans for peak usage.
// We do NOT own the stack memory — the base address is obtained from
// threadGetSelf()->stack_mirror so we never touch the original allocation that
// libnx hands to svcMapMemory (which changes its permissions).
class StackThread {
    pthread_t tid_{};
    bool valid_ = false;

#ifdef STACK_THREAD_MEASURE
    const char* name_      = nullptr;
    void*       stack_mem_ = nullptr; // base of painted region (not owned)
    size_t      stack_size_= 0;       // bytes painted
    size_t      peak_used_ = 0;       // pre-computed before thread exit; valid after join()
    static constexpr unsigned char kMagic = 0xCD;
    static constexpr size_t kPaintGuard   = 2048;
#endif

    struct Payload {
        std::function<void()> fn;
        const char* name;
#ifdef STACK_THREAD_MEASURE
        StackThread* owner;
#endif
    };

    // This structure is exactly 0x20 bytes
    typedef struct {
        // Magic value used to check if the struct is initialized
        u32 magic;

        // Thread handle, for mutexes
        Handle handle;

        // Pointer to the current thread (if exists)
        Thread* thread_ptr;

        // Pointer to this thread's newlib state
        struct _reent* reent;

        // Pointer to this thread's thread-local segment
        void* tls_tp; // !! Offset needs to be TLS+0x1F8 for __aarch64_read_tp !!
    } ThreadVars;

    static inline ThreadVars* getThreadVars(void) {
        return (ThreadVars*)((u8*)armGetTls() + 0x200 - sizeof(ThreadVars));
    }


    static void* trampoline(void* arg) {
        auto* p = static_cast<Payload*>(arg);

#ifdef STACK_THREAD_MEASURE
        // Obtain the actual kernel stack via libnx — this is the mirrored
        // address in the Stack Region, which remains readable unlike the
        // original backing memory that svcMapMemory may have locked.
#ifdef __SWITCH__
        Thread* t        = getThreadVars()->thread_ptr;
        void*   base     = t->stack_mirror;
        size_t  total    = t->stack_sz;
#else
        // Linux fallback: derive base from a local-var SP approximation.
        volatile char here{};
        auto sp    = reinterpret_cast<uintptr_t>(&here);
        auto total = p->owner->stack_size_; // still holds the requested size
        auto base  = reinterpret_cast<void*>(sp - total + 4096);
        total     -= 4096;
#endif
        if (total > kPaintGuard) {
            const size_t paint = total - kPaintGuard;
            memset(base, kMagic, paint);
            p->owner->stack_mem_  = base;
            p->owner->stack_size_ = paint;
        }
#endif // STACK_THREAD_MEASURE

        p->fn();

#ifdef STACK_THREAD_MEASURE
        // Pre-compute peak while the stack mirror is still mapped.
        // After this function returns, libnx calls svcUnmapMemory and
        // stack_mem_ becomes inaccessible — so we must not scan it after join().
        if (p->owner->stack_mem_) {
            const auto* s = static_cast<const unsigned char*>(p->owner->stack_mem_);
            size_t untouched = 0;
            while (untouched < p->owner->stack_size_ && s[untouched] == kMagic)
                ++untouched;
            p->owner->peak_used_ = p->owner->stack_size_ - untouched;
        }
#endif

        delete p;
        return nullptr;
    }

    template<typename F, typename... Args>
    void init(size_t stack_size, const char* name, F&& f, Args&&... args) {
        auto* p = new Payload{
            std::function<void()>{
                [f = std::forward<F>(f),
                 args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                    std::apply(f, std::move(args));
                }},
            name
#ifdef STACK_THREAD_MEASURE
            , this
#endif
        };

#ifdef STACK_THREAD_MEASURE
        name_       = name;
        stack_size_ = stack_size; // used as hint on non-Switch platforms
#endif

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stack_size);
        if (pthread_create(&tid_, &attr, trampoline, p) == 0)
            valid_ = true;
        else
            delete p;
        pthread_attr_destroy(&attr);
    }

public:
    StackThread() = default;
    ~StackThread() { assert(!valid_ && "StackThread destroyed while joinable"); }

    StackThread(const StackThread&) = delete;
    StackThread& operator=(const StackThread&) = delete;

    StackThread(StackThread&& o) noexcept
        : tid_(o.tid_), valid_(o.valid_)
#ifdef STACK_THREAD_MEASURE
        , name_(o.name_), stack_mem_(o.stack_mem_), stack_size_(o.stack_size_), peak_used_(o.peak_used_)
#endif
    {
        o.valid_ = false;
#ifdef STACK_THREAD_MEASURE
        o.stack_mem_ = nullptr;
        o.peak_used_ = 0;
#endif
    }

    StackThread& operator=(StackThread&& o) noexcept {
        assert(!valid_);
        tid_   = o.tid_;
        valid_ = o.valid_;
        o.valid_ = false;
#ifdef STACK_THREAD_MEASURE
        name_       = o.name_;
        stack_mem_  = o.stack_mem_;
        stack_size_ = o.stack_size_;
        peak_used_  = o.peak_used_;
        o.stack_mem_ = nullptr;
        o.peak_used_ = 0;
#endif
        return *this;
    }

    // Without name
    template<typename F, typename... Args>
    StackThread(size_t stack_size, F&& f, Args&&... args) {
        init(stack_size, nullptr, std::forward<F>(f), std::forward<Args>(args)...);
    }

    // With name — const char* as second arg disambiguates from the callable overload
    template<typename F, typename... Args>
    StackThread(size_t stack_size, const char* name, F&& f, Args&&... args) {
        init(stack_size, name, std::forward<F>(f), std::forward<Args>(args)...);
    }

    [[nodiscard]] bool joinable() const noexcept { return valid_; }

    // Safe to call from any thread, including from within the thread itself.
    void dump_meminfo() const {
#ifdef STACK_THREAD_MEASURE
        if (!stack_mem_) return;
        size_t used;
        if (!valid_) {
            // Thread has exited; stack_mirror is unmapped — use pre-computed value.
            used = peak_used_;
        } else {
            const auto* p = static_cast<const unsigned char*>(stack_mem_);
            size_t untouched = 0;
            while (untouched < stack_size_ && p[untouched] == kMagic)
                ++untouched;
            used = stack_size_ - untouched;
        }
        Logger::info("[StackMeasure/%s] peak %zu B / %zu B (%.0f%%)",
                     name_ ? name_ : "?", used, stack_size_,
                     100.0 * used / stack_size_);
#endif
    }

    void join() {
        pthread_join(tid_, nullptr);
        valid_ = false;
        dump_meminfo();
    }

    void detach() {
        pthread_detach(tid_);
        valid_ = false;
    }
};
