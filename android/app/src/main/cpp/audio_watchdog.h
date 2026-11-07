#pragma once
#include <cstdint>
#include <chrono>
#include <atomic>
#include "audio_mixer.h"

namespace nfs_audio_watchdog {

// Record a successful AAudio callback render tick.
void notifyCallbackRendered();

// Service synthetic wall-clock time progression if AAudio callback is delayed or stalled
// (e.g. during screen recorder attach, incoming call, or audio routing renegotiation).
void serviceSyntheticClock(PcmMixer& mixer);

// Cooldown tracker for AAudio stream reconnection to avoid audio HAL thrashing.
bool canAttemptReconnect();
void recordReconnectAttempt();

} // namespace nfs_audio_watchdog
