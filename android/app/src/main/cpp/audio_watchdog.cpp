#include "audio_watchdog.h"
#include <algorithm>
#include <android/log.h>

namespace nfs_audio_watchdog {

static std::atomic<int64_t> s_lastCallbackTimeUs{0};
static std::atomic<int64_t> s_lastSyntheticTickUs{0};
static std::atomic<int64_t> s_lastReconnectAttemptUs{0};

static inline int64_t currentSteadyUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void notifyCallbackRendered() {
    int64_t now = currentSteadyUs();
    s_lastCallbackTimeUs.store(now, std::memory_order_relaxed);
    s_lastSyntheticTickUs.store(now, std::memory_order_relaxed);
}

void serviceSyntheticClock(PcmMixer& mixer) {
    int64_t now = currentSteadyUs();
    int64_t lastCb = s_lastCallbackTimeUs.load(std::memory_order_relaxed);
    if (lastCb == 0) {
        // Initial setup before any audio callback
        s_lastCallbackTimeUs.store(now, std::memory_order_relaxed);
        s_lastSyntheticTickUs.store(now, std::memory_order_relaxed);
        return;
    }

    int64_t sinceCallback = now - lastCb;
    // If the AAudio callback has not fired for more than 20ms (e.g. screen recording started,
    // audio policy handed off to screen capture, incoming call, or bluetooth disconnect),
    // we advance DirectSound buffer positions based on real elapsed time.
    if (sinceCallback > 20000) {
        int64_t lastTick = s_lastSyntheticTickUs.load(std::memory_order_relaxed);
        if (lastTick == 0) lastTick = lastCb;
        int64_t deltaUs = now - lastTick;

        // Clamp delta to avoid huge sudden time jumps if process was suspended
        deltaUs = std::clamp<int64_t>(deltaUs, 0, 100000); // max 100ms per tick

        if (deltaUs >= 5000) { // at least 5ms step
            uint32_t rate = mixer.rate ? mixer.rate : 48000;
            unsigned missedFrames = static_cast<unsigned>((deltaUs * rate) / 1000000ULL);
            if (missedFrames > 0) {
                mixer.advance(missedFrames);
            }
            s_lastSyntheticTickUs.store(now, std::memory_order_relaxed);
        }
    }
}

bool canAttemptReconnect() {
    int64_t now = currentSteadyUs();
    int64_t last = s_lastReconnectAttemptUs.load(std::memory_order_relaxed);
    // 300 ms cooldown between stream reconnection attempts
    return (now - last) >= 300000;
}

void recordReconnectAttempt() {
    s_lastReconnectAttemptUs.store(currentSteadyUs(), std::memory_order_relaxed);
}

} // namespace nfs_audio_watchdog
