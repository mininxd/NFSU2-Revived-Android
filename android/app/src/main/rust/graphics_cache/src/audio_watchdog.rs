//! Safe Rust implementation of the AAudio watchdog and synthetic DirectSound clock.
//! Replaces android/app/src/main/cpp/audio_watchdog.cpp with atomic synchronization.

use std::sync::atomic::{AtomicI64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static LAST_CALLBACK_TIME_US: AtomicI64 = AtomicI64::new(0);
static LAST_SYNTHETIC_TICK_US: AtomicI64 = AtomicI64::new(0);
static LAST_RECONNECT_ATTEMPT_US: AtomicI64 = AtomicI64::new(0);

#[inline]
pub fn current_steady_us() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_micros() as i64
}

/// Records that a real AAudio audio callback has rendered a buffer frame.
pub fn notify_callback_rendered() {
    let now = current_steady_us();
    LAST_CALLBACK_TIME_US.store(now, Ordering::Relaxed);
    LAST_SYNTHETIC_TICK_US.store(now, Ordering::Relaxed);
}

/// Checks if the audio stream was active but has stopped firing for > 250ms
/// (e.g. screen recording started, incoming phone call, or Bluetooth disconnect).
pub fn is_callback_stalled() -> bool {
    let now = current_steady_us();
    let last_cb = LAST_CALLBACK_TIME_US.load(Ordering::Relaxed);
    if last_cb == 0 {
        return false;
    }
    (now - last_cb) > 250_000 // 250ms
}

/// Checks if enough time has passed to attempt reconnecting AAudio without thrashing audio HAL.
pub fn can_attempt_reconnect() -> bool {
    let now = current_steady_us();
    let last = LAST_RECONNECT_ATTEMPT_US.load(Ordering::Relaxed);
    if last == 0 {
        return true;
    }
    (now - last) > 1_000_000 // 1 second cooldown
}

/// Records an AAudio stream reconnection attempt.
pub fn record_reconnect_attempt() {
    let now = current_steady_us();
    LAST_RECONNECT_ATTEMPT_US.store(now, Ordering::Relaxed);
}

/// Calculates synthetic frame progression if real callback has stalled (> 20ms).
/// Returns the number of missed frames to advance.
pub fn calculate_synthetic_advance(sample_rate: u32) -> u32 {
    let now = current_steady_us();
    let last_cb = LAST_CALLBACK_TIME_US.load(Ordering::Relaxed);
    if last_cb == 0 {
        LAST_CALLBACK_TIME_US.store(now, Ordering::Relaxed);
        LAST_SYNTHETIC_TICK_US.store(now, Ordering::Relaxed);
        return 0;
    }

    let since_callback = now - last_cb;
    if since_callback > 20_000 {
        // Callback hasn't fired in > 20ms
        let mut last_tick = LAST_SYNTHETIC_TICK_US.load(Ordering::Relaxed);
        if last_tick == 0 {
            last_tick = last_cb;
        }
        let delta_us = (now - last_tick).clamp(0, 100_000); // max 100ms clamp

        if delta_us >= 5_000 { // at least 5ms step
            let rate = if sample_rate != 0 { sample_rate as u64 } else { 48_000 };
            let missed_frames = ((delta_us as u64 * rate) / 1_000_000) as u32;
            LAST_SYNTHETIC_TICK_US.store(now, Ordering::Relaxed);
            return missed_frames;
        }
    }
    0
}

// ---------------------------------------------------------------------------
// C ABI Compatibility Exports
// ---------------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn rust_audio_watchdog_notify() {
    notify_callback_rendered();
}

#[no_mangle]
pub extern "C" fn rust_audio_watchdog_stalled() -> i32 {
    if is_callback_stalled() { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn rust_audio_watchdog_can_reconnect() -> i32 {
    if can_attempt_reconnect() { 1 } else { 0 }
}

#[no_mangle]
pub extern "C" fn rust_audio_watchdog_record_reconnect() {
    record_reconnect_attempt();
}

#[no_mangle]
pub extern "C" fn rust_audio_watchdog_synthetic_advance(sample_rate: u32) -> u32 {
    calculate_synthetic_advance(sample_rate)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_watchdog_lifecycle() {
        notify_callback_rendered();
        assert!(!is_callback_stalled());
        assert!(can_attempt_reconnect());
        record_reconnect_attempt();
        assert!(!can_attempt_reconnect()); // Cooldown active
    }
}
