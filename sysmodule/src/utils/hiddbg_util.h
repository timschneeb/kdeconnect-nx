#pragma once

#ifdef __SWITCH__
#include <switch/types.h>

/// State for overriding CaptureButtonState.
typedef struct {
    u64 buttons;                                ///< Bitfield of buttons, only bit0 is used.
} HiddbgCaptureButtonAutoPilotState;

// Reference-counted hiddbg lifetime shared across plugins (mousepad, share, etc.).
// Call retain() before any hiddbg API use and release() when done.
void hiddbg_retain();
void hiddbg_release();
bool hiddbg_is_available();

Result hiddbgSetCaptureButtonAutoPilotState(const HiddbgCaptureButtonAutoPilotState *state);
Result hiddbgUnsetCaptureButtonAutoPilotState();
#endif // __SWITCH__
