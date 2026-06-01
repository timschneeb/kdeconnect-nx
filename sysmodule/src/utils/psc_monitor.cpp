#include "psc_monitor.h"
#include "logger.h"

static constexpr u32 kDeps[] = { PscPmModuleId_Fs };
static std::atomic_bool s_is_awake = true;

void PscMonitor::thread_func(void* arg) {
    auto* self = static_cast<PscMonitor*>(arg);
    Waiter waiter = waiterForEvent(&self->module_.event);

    while (self->running_) {
        if (R_FAILED(waitSingle(waiter, UINT64_MAX)))
            break;

        PscPmState state;
        u32 flags;
        if (R_FAILED(pscPmModuleGetRequest(&self->module_, &state, &flags)))
            continue;

        pscPmModuleAcknowledge(&self->module_, state);

        switch (state) {
            case PscPmState_Awake:
            case PscPmState_ReadyAwaken:
            case PscPmState_ReadyAwakenCritical:
                s_is_awake.store(true);
                break;
            case PscPmState_ReadySleep:
            case PscPmState_ReadySleepCritical:
                s_is_awake.store(false);
                break;
            case PscPmState_ReadyShutdown:
                s_is_awake.store(false);
                self->running_ = false;
                break;
        }
    }
}

void PscMonitor::start() {
    if (running_.exchange(true)) return;

    Result rc = pscmInitialize();
    if (R_FAILED(rc)) {
        Logger::error("PscMonitor: pscmInitialize: 0x%x", rc);
        running_ = false;
        return;
    }

    rc = pscmGetPmModule(&module_, static_cast<PscPmModuleId>(252),
                         kDeps, std::size(kDeps), true);
    if (R_FAILED(rc)) {
        Logger::error("PscMonitor: pscmGetPmModule: 0x%x", rc);
        pscmExit();
        running_ = false;
        return;
    }

    rc = threadCreate(&thread_, thread_func, this, nullptr, 0x1000, 0x2C, -2);
    if (R_FAILED(rc)) {
        Logger::error("PscMonitor: threadCreate: 0x%x", rc);
        pscPmModuleFinalize(&module_);
        pscPmModuleClose(&module_);
        eventClose(&module_.event);
        pscmExit();
        running_ = false;
        return;
    }

    rc = threadStart(&thread_);
    if (R_FAILED(rc)) {
        Logger::error("PscMonitor: threadStart: 0x%x", rc);
        threadClose(&thread_);
        pscPmModuleFinalize(&module_);
        pscPmModuleClose(&module_);
        eventClose(&module_.event);
        pscmExit();
        running_ = false;
    }
}

void PscMonitor::stop() {
    if (!running_.exchange(false)) return;

    pscPmModuleFinalize(&module_);
    pscPmModuleClose(&module_);
    eventClose(&module_.event);

    if (thread_.handle)
        svcCancelSynchronization(thread_.handle);
    threadWaitForExit(&thread_);
    threadClose(&thread_);

    pscmExit();
}

bool PscMonitor::is_awake() {
    return s_is_awake.load();
}
