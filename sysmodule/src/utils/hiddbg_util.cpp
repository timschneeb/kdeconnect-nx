#include "hiddbg_util.h"

#ifdef __SWITCH__
#include <atomic>
#include <switch.h>
#include "logger.h"

static std::atomic s_hiddbg_refcount{0};

void hiddbg_retain() {
    if (s_hiddbg_refcount.fetch_add(1) == 0) {
        Result rc = hiddbgInitialize();
        if (R_FAILED(rc)) {
            s_hiddbg_refcount.fetch_sub(1);
            Logger::error("hiddbgInitialize failed: 0x%x", rc);
        }
    }
}

void hiddbg_release() {
    if (s_hiddbg_refcount.fetch_sub(1) == 1) {
        hiddbgExit();
    }
}

bool hiddbg_is_available() {
    return s_hiddbg_refcount.load() > 0;
}

Result hiddbgSetCaptureButtonAutoPilotState(const HiddbgCaptureButtonAutoPilotState *state) {
    return serviceDispatchIn(hiddbgGetServiceSession(), 131, *state);
}

Result hiddbgUnsetCaptureButtonAutoPilotState() {
    return serviceDispatch(hiddbgGetServiceSession(), 132);
}

#endif // __SWITCH__
