#pragma once
#include <cstdint>

namespace nfs_gpu {

/**
 * Initializes the GPU DVFS governor and frequency sustainer engine.
 */
void init();

/**
 * Commands an immediate peak clock frequency lock and extends idle_timer.
 * @param isGameplay True if currently in active race or open-world roam.
 */
void lockPeakClock(bool isGameplay);

/**
 * Sets the active gameplay state (e.g. In Race / In Roam vs Garage / Menus).
 */
void setGameplayState(bool inGameplay);

/**
 * Per-frame tick hook called after Present() to prevent GPU downclocking
 * during the x86 CPU emulation interval.
 */
void onFrameTick(bool inGameplay);

/**
 * Returns the currently locked GPU clock frequency in MHz (or 0 if unknown).
 */
int getLockedFreqMhz();

} // namespace nfs_gpu
