#pragma once

#ifdef __SWITCH__

// Reference-counted hiddbg lifetime shared across plugins (mousepad, share, etc.).
// Call retain() before any hiddbg API use and release() when done.
void hiddbg_retain();
void hiddbg_release();
bool hiddbg_is_available();

#endif // __SWITCH__
