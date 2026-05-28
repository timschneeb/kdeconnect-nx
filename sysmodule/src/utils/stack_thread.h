#pragma once

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <pthread.h>
#include <switch/arm/tls.h>
#include <switch/kernel/thread.h>

#include "logger.h"

#ifdef DEBUG
#define STACK_THREAD_MEASURE
#endif

// Drop-in replacement for std::thread with an explicit stack size.
// Move-only; must be joined or detached before destruction.
//
// Build with -DSTACK_THREAD_MEASURE to enable stack painting.
// The trampoline paints the stack, stores the region info in the Payload, and
// pre-computes peak usage after fn() returns, without touching the owner
// StackThread pointer, so it is safe even if the owning object is destroyed
// concurrently (e.g. DeviceSession torn down while io_thread still runs).
// dump_meminfo() reads directly from the Payload during execution, or uses the
// pre-computed value after join().
class StackThread {
    pthread_t tid_{};
    bool valid_ = false;

#ifdef STACK_THREAD_MEASURE
    const char* name_      = nullptr;
    size_t      peak_used_ = 0;       // valid after join(); copied from payload
    static constexpr unsigned char kMagic = 0xCD;
    static constexpr size_t kPaintGuard   = 2048;
#endif

    struct Payload {
        std::function<void()> fn;
        const char* name;
#ifdef STACK_THREAD_MEASURE
        size_t      stack_size_hint = 0; // requested stack size
        void*       paint_base      = nullptr; // written once at thread start; read by dump_meminfo
        size_t      paint_size      = 0;
        size_t      peak_computed   = 0; // written after fn() returns; read by join()
#endif
    };

#ifdef STACK_THREAD_MEASURE
    Payload* payload_ = nullptr; // owned; freed by join() (not by trampoline)
#endif

    // This structure is exactly 0x20 bytes
    typedef struct {
        u32     magic;
        Handle  handle;
        Thread* thread_ptr;
        struct _reent* reent;
        void*   tls_tp;
    } ThreadVars;

    static ThreadVars* getThreadVars() {
        return reinterpret_cast<ThreadVars *>(static_cast<u8 *>(armGetTls()) + 0x200 - sizeof(ThreadVars));
    }

    static void* trampoline(void* arg) {
        auto* p = static_cast<Payload*>(arg);

#ifdef STACK_THREAD_MEASURE
#ifdef __SWITCH__
        Thread* t     = getThreadVars()->thread_ptr;
        void*   base  = t->stack_mirror;
        size_t  total = t->stack_sz;
#else
        #error Stack measurement is only supported on Switch
#endif
        if (total > kPaintGuard) {
            const size_t paint = total - kPaintGuard;
            memset(base, kMagic, paint);
            p->paint_base = base;
            p->paint_size = paint;
        }
#endif // STACK_THREAD_MEASURE

        p->fn();

#ifdef STACK_THREAD_MEASURE
        if (p->paint_base) {
            const auto* s = static_cast<const unsigned char*>(p->paint_base);
            size_t untouched = 0;
            while (untouched < p->paint_size && s[untouched] == kMagic)
                ++untouched;
            p->peak_computed = p->paint_size - untouched;
        }
        // Do not delete p: join() reads peak_computed and then frees it.
        return nullptr;
#else
        delete p;
        return nullptr;
#endif
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
            , stack_size  // stack_size_hint
#endif
        };

#ifdef STACK_THREAD_MEASURE
        name_    = name;
        payload_ = p;
#endif

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stack_size);
        if (pthread_create(&tid_, &attr, trampoline, p) == 0) {
            valid_ = true;
        } else {
            delete p;
#ifdef STACK_THREAD_MEASURE
            payload_ = nullptr;
#endif
        }
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
        , name_(o.name_), peak_used_(o.peak_used_), payload_(o.payload_)
#endif
    {
        o.valid_ = false;
#ifdef STACK_THREAD_MEASURE
        o.peak_used_ = 0;
        o.payload_   = nullptr;
#endif
    }

    StackThread& operator=(StackThread&& o) noexcept {
        assert(!valid_);
        tid_   = o.tid_;
        valid_ = o.valid_;
        o.valid_ = false;
#ifdef STACK_THREAD_MEASURE
        name_     = o.name_;
        peak_used_ = o.peak_used_;
        payload_   = o.payload_;
        o.peak_used_ = 0;
        o.payload_   = nullptr;
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
        size_t used, total;
        if (!valid_) {
            // Thread has exited; mirror is unmapped: use value pre-computed in trampoline.
            if (!peak_used_) return;
            used  = peak_used_;
            total = payload_ ? payload_->paint_size : peak_used_; // payload_ freed by join
        } else {
            if (!payload_ || !payload_->paint_base) return;
            const auto* p = static_cast<const unsigned char*>(payload_->paint_base);
            size_t untouched = 0;
            while (untouched < payload_->paint_size && p[untouched] == kMagic)
                ++untouched;
            used  = payload_->paint_size - untouched;
            total = payload_->paint_size;
        }
        Logger::info("[StackMeasure/%s%s] peak %zu B / %zu B (%.0f%%)",
                     name_ ? name_ : "?", valid_ ? "" : "(dead)", used, total, 100.0 * used / total);
#endif
    }

    void join() {
        pthread_join(tid_, nullptr);
        valid_ = false;
#ifdef STACK_THREAD_MEASURE
        if (payload_) {
            peak_used_ = payload_->peak_computed;
            delete payload_;
            payload_ = nullptr;
        }
#endif
        dump_meminfo();
    }

    void detach() {
        pthread_detach(tid_);
        valid_ = false;
#ifdef STACK_THREAD_MEASURE
        // Measurement is not meaningful for detached threads. The trampoline
        // still holds `p` and will not free it, so we must free it here.
        // Narrow race: if fn() just returned and the trampoline is writing
        // peak_computed, this is benign — we only lose the measurement result.
        delete payload_;
        payload_ = nullptr;
#endif
    }
};
